/*
 * BrewTube - A Homebrew YouTube App for the Wii
 * Copyright (C) 2026  ReviveMii Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audioconvert.h>

#include "decoder.h"
#include "netstream.h"

#define IO_BUFFER_SIZE (128 * 1024)
#define MAX_CHANNELS 8
#define MAX_VIDEO_WIDTH 1024
#define MAX_VIDEO_HEIGHT 720
#define SEEK_TOLERANCE 0.02
#define PTS_RESYNC 0.05

struct Decoder {
	int fd;
	NetStream *net;
	AVFormatContext *fmt;
	AVIOContext *io;
	AVCodecContext *vctx;
	AVCodecContext *actx;
	AVFrame *frame;
	AVPacket orig;
	AVPacket pkt;
	int havePkt;
	int draining;
	int vIndex, aIndex;
	double startTime;
	double frameDuration;
	double lastVideoPts;
	double seekV, seekA;
	double nextAudioPts;
	double pendingAudioPts;
	int audioPtsValid;
	int pendingValid;
	float mixL[MAX_CHANNELS];
	float mixR[MAX_CHANNELS];
	int16_t *pcm;
	int pcmCap;
	char error[128];

	NetStream *netAudio;
	AVFormatContext *fmtAudio;
	AVIOContext *ioAudio;
	AVPacket origAudio;
	AVPacket pktAudio;
	int havePktAudio;
	int audioEof;
	int videoEof;
	int abort;
};

static int checkInterrupt(void *opaque)
{
	Decoder *d = (Decoder *)opaque;
	return (d && d->abort) ? 1 : 0;
}

static int ioRead(void *opaque, uint8_t *buf, int size)
{
	Decoder *d = opaque;

	if (d->abort)
		return 0;

	if (d->net)
		return netRead(d->net, buf, size);

	int n = read(d->fd, buf, size);
	return n < 0 ? 0 : n;
}

static int64_t ioSeek(void *opaque, int64_t offset, int whence)
{
	Decoder *d = opaque;

	if (whence & AVSEEK_SIZE) {
		if (d->net)
			return netSize(d->net);
		struct stat st;
		return fstat(d->fd, &st) == 0 ? st.st_size : -1;
	}

	int w = whence & ~AVSEEK_FORCE;
	if (d->net)
		return netSeek(d->net, offset, w);

	off_t r = lseek(d->fd, offset, w);
	return r < 0 ? -1 : r;
}

static int ioReadAudio(void *opaque, uint8_t *buf, int size)
{
	Decoder *d = opaque;
	if (d->abort)
		return 0;
	if (d->netAudio)
		return netRead(d->netAudio, buf, size);
	return 0;
}

static int64_t ioSeekAudio(void *opaque, int64_t offset, int whence)
{
	Decoder *d = opaque;
	if (whence & AVSEEK_SIZE) {
		if (d->netAudio)
			return netSize(d->netAudio);
		return -1;
	}
	int w = whence & ~AVSEEK_FORCE;
	if (d->netAudio)
		return netSeek(d->netAudio, offset, w);
	return -1;
}

static void setText(char *dst, AVDictionary *meta, const char *key)
{
	AVDictionaryEntry *e = meta ? av_dict_get(meta, key, NULL, 0) : NULL;
	if (e && e->value)
		snprintf(dst, DEC_TEXT_LEN, "%s", e->value);
}

static void setMix(Decoder *d)
{
	static const float coef[][2] = {
		{1, 0}, {0, 1}, {0.707f, 0.707f}, {0, 0}, {0.707f, 0}, {0, 0.707f},
		{0.707f, 0}, {0, 0.707f}, {0.5f, 0.5f}, {0.707f, 0}, {0, 0.707f}
	};
	uint64_t layout = d->actx->channel_layout;
	int channels = d->actx->channels;
	int i = 0;
	float sumL = 0, sumR = 0;

	if (!layout)
		layout = av_get_default_channel_layout(channels);

	for (int bit = 0; bit < 11 && i < channels; bit++) {
		if (!(layout & (1ULL << bit)))
			continue;
		d->mixL[i] = coef[bit][0];
		d->mixR[i] = coef[bit][1];
		sumL += d->mixL[i];
		sumR += d->mixR[i];
		i++;
	}

	float norm = sumL == 0 && sumR == 0 ? 1.0f : 1.0f / (sumL > sumR ? sumL : sumR);

	for (i = 0; i < channels; i++) {
		d->mixL[i] *= norm;
		d->mixR[i] *= norm;
	}
}

static int openCodec(AVCodecContext *ctx)
{
	AVCodec *codec = avcodec_find_decoder(ctx->codec_id);
	ctx->thread_count = 1;
	return codec && avcodec_open2(ctx, codec, NULL) >= 0;
}

Decoder *decOpen(const char *path, MediaInfo *info, char *err, int errSize)
{
	static int registered = 0;
	Decoder *d = calloc(1, sizeof(Decoder));
	const char *msg = "Could not open file";

	if (!d)
		return NULL;

	if (!registered) {
		av_register_all();
		av_log_set_level(AV_LOG_ERROR);
		registered = 1;
	}

	memset(info, 0, sizeof(*info));
	d->vIndex = d->aIndex = -1;
	d->seekV = d->seekA = -1;

	const char *sep = strchr(path, '\n');
	if (sep) {
		char vPath[4096];
		char aPath[4096];
		size_t vLen = sep - path;
		if (vLen >= sizeof(vPath)) vLen = sizeof(vPath) - 1;
		memcpy(vPath, path, vLen);
		vPath[vLen] = '\0';
		snprintf(aPath, sizeof(aPath), "%s", sep + 1);

		d->net = netOpen(vPath, err, errSize);
		if (!d->net) {
			free(d);
			return NULL;
		}

		uint8_t *buf = av_malloc(IO_BUFFER_SIZE);
		d->io = buf ? avio_alloc_context(buf, IO_BUFFER_SIZE, 0, d, ioRead, NULL, ioSeek) : NULL;
		d->fmt = avformat_alloc_context();
		if (!d->io || !d->fmt)
			goto fail;

		d->fmt->pb = d->io;
		d->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
		d->fmt->interrupt_callback.callback = checkInterrupt;
		d->fmt->interrupt_callback.opaque = d;

		msg = "Unsupported video format";
		if (avformat_open_input(&d->fmt, vPath, NULL, NULL) < 0)
			goto fail;
		if (avformat_find_stream_info(d->fmt, NULL) < 0)
			goto fail;

		for (unsigned i = 0; i < d->fmt->nb_streams; i++) {
			AVStream *st = d->fmt->streams[i];
			AVCodecContext *c = st->codec;

			if (c->codec_type == AVMEDIA_TYPE_VIDEO && d->vIndex < 0 && c->codec_id == CODEC_ID_H264)
				d->vIndex = i;
			else
				st->discard = AVDISCARD_ALL;
		}

		if (d->vIndex < 0) {
			msg = "No H.264 video stream found";
			goto fail;
		}

		d->netAudio = netOpenEx(aPath, err, errSize, 256 * 1024);
		if (!d->netAudio)
			goto fail;

		uint8_t *bufA = av_malloc(IO_BUFFER_SIZE);
		d->ioAudio = bufA ? avio_alloc_context(bufA, IO_BUFFER_SIZE, 0, d, ioReadAudio, NULL, ioSeekAudio) : NULL;
		d->fmtAudio = avformat_alloc_context();
		if (!d->ioAudio || !d->fmtAudio)
			goto fail;

		d->fmtAudio->pb = d->ioAudio;
		d->fmtAudio->flags |= AVFMT_FLAG_CUSTOM_IO;
		d->fmtAudio->interrupt_callback.callback = checkInterrupt;
		d->fmtAudio->interrupt_callback.opaque = d;

		msg = "Unsupported audio format";
		if (avformat_open_input(&d->fmtAudio, aPath, NULL, NULL) < 0)
			goto fail;
		if (avformat_find_stream_info(d->fmtAudio, NULL) < 0)
			goto fail;

		for (unsigned i = 0; i < d->fmtAudio->nb_streams; i++) {
			AVStream *st = d->fmtAudio->streams[i];
			AVCodecContext *c = st->codec;

			if (c->codec_type == AVMEDIA_TYPE_AUDIO && d->aIndex < 0 &&
				(c->codec_id == CODEC_ID_AAC || c->codec_id == CODEC_ID_MP3))
				d->aIndex = i;
			else
				st->discard = AVDISCARD_ALL;
		}

		if (d->aIndex < 0) {
			msg = "No AAC/MP3 audio stream found";
			goto fail;
		}
	} else {
		if (netIsUrl(path)) {
			d->net = netOpen(path, err, errSize);
			if (!d->net) {
				free(d);
				return NULL;
			}
		} else {
			d->fd = open(path, O_RDONLY);
			if (d->fd < 0)
				goto fail;
		}

		uint8_t *buf = av_malloc(IO_BUFFER_SIZE);
		d->io = buf ? avio_alloc_context(buf, IO_BUFFER_SIZE, 0, d, ioRead, NULL, ioSeek) : NULL;
		d->fmt = avformat_alloc_context();
		if (!d->io || !d->fmt)
			goto fail;

		d->fmt->pb = d->io;
		d->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
		d->fmt->interrupt_callback.callback = checkInterrupt;
		d->fmt->interrupt_callback.opaque = d;

		msg = "Unsupported file format";
		if (avformat_open_input(&d->fmt, path, NULL, NULL) < 0)
			goto fail;
		if (avformat_find_stream_info(d->fmt, NULL) < 0)
			goto fail;

		for (unsigned i = 0; i < d->fmt->nb_streams; i++) {
			AVStream *st = d->fmt->streams[i];
			AVCodecContext *c = st->codec;

			if (c->codec_type == AVMEDIA_TYPE_VIDEO && d->vIndex < 0 && c->codec_id == CODEC_ID_H264)
				d->vIndex = i;
			else if (c->codec_type == AVMEDIA_TYPE_AUDIO && d->aIndex < 0 &&
				(c->codec_id == CODEC_ID_AAC || c->codec_id == CODEC_ID_MP3))
				d->aIndex = i;
			else
				st->discard = AVDISCARD_ALL;
		}

		if (d->vIndex < 0 && d->aIndex < 0) {
			msg = "No H.264 video or AAC/MP3 audio stream found";
			goto fail;
		}
	}

	d->startTime = d->fmt->start_time != AV_NOPTS_VALUE ? (double)d->fmt->start_time / AV_TIME_BASE : 0;

	if (d->vIndex >= 0) {
		AVStream *st = d->fmt->streams[d->vIndex];
		d->vctx = st->codec;
		d->vctx->flags2 |= CODEC_FLAG2_FAST;

		if (d->vctx->width > MAX_VIDEO_WIDTH || d->vctx->height > MAX_VIDEO_HEIGHT) {
			msg = "Video resolution too high (max 1024x720)";
			goto fail;
		}
		if (d->vctx->pix_fmt != PIX_FMT_NONE && d->vctx->pix_fmt != PIX_FMT_YUV420P && d->vctx->pix_fmt != PIX_FMT_YUVJ420P) {
			msg = "Unsupported H.264 pixel format (only 4:2:0)";
			goto fail;
		}
		if (!openCodec(d->vctx)) {
			msg = "Could not open H.264 decoder";
			goto fail;
		}

		AVRational fps = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
		AVRational sar = st->sample_aspect_ratio.num ? st->sample_aspect_ratio : d->vctx->sample_aspect_ratio;
		int validSar = sar.num > 0 && sar.den > 0;

		d->frameDuration = fps.num ? (double)fps.den / fps.num : 1.0 / 25;
		info->hasVideo = 1;
		info->width = d->vctx->width;
		info->height = d->vctx->height;
		info->sarNum = validSar ? sar.num : 1;
		info->sarDen = validSar ? sar.den : 1;
	}

	if (d->aIndex >= 0) {
		AVFormatContext *afmt = d->fmtAudio ? d->fmtAudio : d->fmt;
		d->actx = afmt->streams[d->aIndex]->codec;
		if (d->actx->channels < 1 || d->actx->channels > MAX_CHANNELS || !openCodec(d->actx)) {
			if (d->vIndex < 0) {
				msg = "Could not open audio decoder";
				goto fail;
			}
			afmt->streams[d->aIndex]->discard = AVDISCARD_ALL;
			d->aIndex = -1;
			d->actx = NULL;
		} else {
			setMix(d);
			info->hasAudio = 1;
			info->sampleRate = d->actx->sample_rate;
		}
	}

	d->frame = avcodec_alloc_frame();
	if (!d->frame)
		goto fail;

	if (d->fmt->duration != AV_NOPTS_VALUE)
		info->duration = (double)d->fmt->duration / AV_TIME_BASE;
	else if (d->fmtAudio && d->fmtAudio->duration != AV_NOPTS_VALUE)
		info->duration = (double)d->fmtAudio->duration / AV_TIME_BASE;

	setText(info->title, d->fmt->metadata, "title");
	setText(info->artist, d->fmt->metadata, "artist");
	setText(info->album, d->fmt->metadata, "album");
	return d;

fail:
	if (err)
		snprintf(err, errSize, "%s", msg);
	decClose(d);
	return NULL;
}

void decAbort(Decoder *d)
{
	if (!d)
		return;

	d->abort = 1;
	if (d->net)
		netAbort(d->net);
	if (d->netAudio)
		netAbort(d->netAudio);
}

void decClose(Decoder *d)
{
	if (!d)
		return;

	decAbort(d);

	if (d->havePkt)
		av_free_packet(&d->orig);
	if (d->havePktAudio)
		av_free_packet(&d->origAudio);

	if (d->fmt)
		avformat_close_input(&d->fmt);
	if (d->io) {
		av_free(d->io->buffer);
		av_free(d->io);
	}
	if (d->net)
		netClose(d->net);
	else if (d->fd > 0)
		close(d->fd);

	if (d->fmtAudio)
		avformat_close_input(&d->fmtAudio);
	if (d->ioAudio) {
		av_free(d->ioAudio->buffer);
		av_free(d->ioAudio);
	}
	if (d->netAudio)
		netClose(d->netAudio);

	av_free(d->frame);
	free(d->pcm);
	free(d);
}

const char *decError(Decoder *d)
{
	return d->error;
}

void decSetFast(Decoder *d, int fast)
{
	if (d->vctx)
		d->vctx->skip_loop_filter = fast ? AVDISCARD_ALL : AVDISCARD_DEFAULT;
}

static void dropPacket(Decoder *d)
{
	if (d->havePkt)
		av_free_packet(&d->orig);
	d->havePkt = 0;
}

static void dropPacketAudio(Decoder *d)
{
	if (d->havePktAudio)
		av_free_packet(&d->origAudio);
	d->havePktAudio = 0;
}

static double toSeconds(Decoder *d, int stream, int64_t ts)
{
	return ts * av_q2d(d->fmt->streams[stream]->time_base) - d->startTime;
}

static double toSecondsAudio(Decoder *d, int stream, int64_t ts)
{
	AVFormatContext *afmt = d->fmtAudio ? d->fmtAudio : d->fmt;
	double aStart = d->fmtAudio && d->fmtAudio->start_time != AV_NOPTS_VALUE ? (double)d->fmtAudio->start_time / AV_TIME_BASE : d->startTime;
	return ts * av_q2d(afmt->streams[stream]->time_base) - aStart;
}

static int videoOut(Decoder *d, DecFrame *out)
{
	int64_t ts = d->frame->best_effort_timestamp;
	double pts = ts != AV_NOPTS_VALUE ? toSeconds(d, d->vIndex, ts) : d->lastVideoPts + d->frameDuration;

	d->lastVideoPts = pts;

	if (d->vctx->pix_fmt != PIX_FMT_YUV420P && d->vctx->pix_fmt != PIX_FMT_YUVJ420P) {
		snprintf(d->error, sizeof(d->error), "Unsupported H.264 pixel format (only 4:2:0)");
		return -1;
	}
	if (pts < d->seekV - SEEK_TOLERANCE)
		return 0;

	d->seekV = -1;
	out->pts = pts;
	for (int i = 0; i < 3; i++) {
		out->planes[i] = d->frame->data[i];
		out->strides[i] = d->frame->linesize[i];
	}
	return 1;
}

static int audioOut(Decoder *d, DecFrame *out)
{
	AVFrame *f = d->frame;
	int n = f->nb_samples;
	int channels = d->actx->channels;
	int fmt = f->format;
	int isFloat = fmt == AV_SAMPLE_FMT_FLT || fmt == AV_SAMPLE_FMT_FLTP;
	int planar = fmt == AV_SAMPLE_FMT_FLTP || fmt == AV_SAMPLE_FMT_S16P;

	if (!isFloat && fmt != AV_SAMPLE_FMT_S16 && fmt != AV_SAMPLE_FMT_S16P) {
		snprintf(d->error, sizeof(d->error), "Unsupported audio sample format");
		return -1;
	}

	if (d->pendingValid) {
		if (!d->audioPtsValid || fabs(d->pendingAudioPts - d->nextAudioPts) > PTS_RESYNC)
			d->nextAudioPts = d->pendingAudioPts;
		d->pendingValid = 0;
	}
	d->audioPtsValid = 1;

	double pts = d->nextAudioPts;
	d->nextAudioPts += (double)n / f->sample_rate;

	if (pts + (double)n / f->sample_rate < d->seekA)
		return 0;
	d->seekA = -1;

	if (n * 2 > d->pcmCap) {
		free(d->pcm);
		d->pcmCap = n * 2;
		d->pcm = malloc(d->pcmCap * sizeof(int16_t));
		if (!d->pcm) {
			d->pcmCap = 0;
			return -1;
		}
	}

	for (int i = 0; i < n; i++) {
		float l = 0, r = 0;

		for (int c = 0; c < channels; c++) {
			float v;
			if (isFloat)
				v = (planar ? ((float *)f->extended_data[c])[i] : ((float *)f->data[0])[i * channels + c]) * 32768.0f;
			else
				v = planar ? ((int16_t *)f->extended_data[c])[i] : ((int16_t *)f->data[0])[i * channels + c];
			l += v * d->mixL[c];
			r += v * d->mixR[c];
		}

		l = l > 32767.0f ? 32767.0f : (l < -32768.0f ? -32768.0f : l);
		r = r > 32767.0f ? 32767.0f : (r < -32768.0f ? -32768.0f : r);
		d->pcm[i * 2] = (int16_t)l;
		d->pcm[i * 2 + 1] = (int16_t)r;
	}

	out->pts = pts;
	out->pcm = d->pcm;
	out->frames = n;
	out->sampleRate = f->sample_rate;
	return 1;
}

DecResult decNext(Decoder *d, DecFrame *out)
{
	for (;;) {
		int got = 0, r;

		if (d->fmtAudio) {
			if (d->videoEof && (d->audioEof || !d->actx))
				return DEC_EOF;

			int wantAudio = d->actx && !d->audioEof && (d->nextAudioPts <= d->lastVideoPts || !d->vctx || d->videoEof);

			if (wantAudio) {
				if (d->havePktAudio) {
					int used = avcodec_decode_audio4(d->actx, d->frame, &got, &d->pktAudio);
					if (used <= 0) {
						dropPacketAudio(d);
						continue;
					}
					d->pktAudio.data += used;
					d->pktAudio.size -= used;
					if (d->pktAudio.size <= 0)
						dropPacketAudio(d);
					if (got && (r = audioOut(d, out)) != 0)
						return r > 0 ? DEC_AUDIO : DEC_ERROR;
					continue;
				}

				if (av_read_frame(d->fmtAudio, &d->origAudio) < 0) {
					d->audioEof = 1;
					continue;
				}

				d->pktAudio = d->origAudio;
				d->havePktAudio = 1;

				if (d->origAudio.stream_index == d->aIndex && d->origAudio.pts != AV_NOPTS_VALUE) {
					d->pendingAudioPts = toSecondsAudio(d, d->aIndex, d->origAudio.pts);
					d->pendingValid = 1;
				} else if (d->origAudio.stream_index != d->aIndex) {
					dropPacketAudio(d);
				}
				continue;
			}

			if (d->havePkt) {
				avcodec_decode_video2(d->vctx, d->frame, &got, &d->pkt);
				dropPacket(d);
				if (got && (r = videoOut(d, out)) != 0)
					return r > 0 ? DEC_VIDEO : DEC_ERROR;
				continue;
			}

			if (d->draining) {
				AVPacket empty;
				av_init_packet(&empty);
				empty.data = NULL;
				empty.size = 0;
				avcodec_decode_video2(d->vctx, d->frame, &got, &empty);
				if (!got) {
					d->videoEof = 1;
					continue;
				}
				if ((r = videoOut(d, out)) != 0)
					return r > 0 ? DEC_VIDEO : DEC_ERROR;
				continue;
			}

			if (av_read_frame(d->fmt, &d->orig) < 0) {
				if (!d->vctx)
					d->videoEof = 1;
				else
					d->draining = 1;
				continue;
			}

			d->pkt = d->orig;
			d->havePkt = 1;

			if (d->orig.stream_index != d->vIndex)
				dropPacket(d);
			continue;
		}

		if (d->havePkt) {
			if (d->pkt.stream_index == d->aIndex) {
				int used = avcodec_decode_audio4(d->actx, d->frame, &got, &d->pkt);
				if (used <= 0) {
					dropPacket(d);
					continue;
				}
				d->pkt.data += used;
				d->pkt.size -= used;
				if (d->pkt.size <= 0)
					dropPacket(d);
				if (got && (r = audioOut(d, out)) != 0)
					return r > 0 ? DEC_AUDIO : DEC_ERROR;
			} else {
				avcodec_decode_video2(d->vctx, d->frame, &got, &d->pkt);
				dropPacket(d);
				if (got && (r = videoOut(d, out)) != 0)
					return r > 0 ? DEC_VIDEO : DEC_ERROR;
			}
			continue;
		}

		if (d->draining) {
			AVPacket empty;
			av_init_packet(&empty);
			empty.data = NULL;
			empty.size = 0;
			avcodec_decode_video2(d->vctx, d->frame, &got, &empty);
			if (!got)
				return DEC_EOF;
			if ((r = videoOut(d, out)) != 0)
				return r > 0 ? DEC_VIDEO : DEC_ERROR;
			continue;
		}

		if (av_read_frame(d->fmt, &d->orig) < 0) {
			if (!d->vctx)
				return DEC_EOF;
			d->draining = 1;
			continue;
		}

		d->pkt = d->orig;
		d->havePkt = 1;

		if (d->orig.stream_index == d->aIndex && d->orig.pts != AV_NOPTS_VALUE) {
			d->pendingAudioPts = toSeconds(d, d->aIndex, d->orig.pts);
			d->pendingValid = 1;
		} else if (d->orig.stream_index != d->aIndex && d->orig.stream_index != d->vIndex) {
			dropPacket(d);
		}
	}
}

int decSeek(Decoder *d, double seconds)
{
	if (seconds < 0)
		seconds = 0;

	int64_t ts = (int64_t)((seconds + d->startTime) * AV_TIME_BASE);
	if (av_seek_frame(d->fmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0)
		return -1;

	dropPacket(d);
	if (d->fmtAudio) {
		av_seek_frame(d->fmtAudio, -1, ts, AVSEEK_FLAG_BACKWARD);
		dropPacketAudio(d);
		d->audioEof = 0;
	}
	d->videoEof = 0;

	if (d->vctx)
		avcodec_flush_buffers(d->vctx);
	if (d->actx)
		avcodec_flush_buffers(d->actx);

	d->draining = 0;
	d->audioPtsValid = 0;
	d->pendingValid = 0;
	d->nextAudioPts = seconds;
	d->lastVideoPts = seconds;
	d->seekV = d->vctx ? seconds : -1;
	d->seekA = d->actx ? seconds : -1;
	return 0;
}
