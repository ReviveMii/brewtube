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

#include <asndlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>

#include "libgui/Gui.h"
#include "libgui/GuiTextRenderer.h"
#include "drivers/Platform.h"
#include "drivers/Thread.h"
#include "drivers/ogc/OgcVideoDriver.h"
#include "menu.h"
#include "filelist.h"
#include "player.h"
#include "decoder.h"
#include "gxyuv.h"
#include "youtube.h"

namespace {

const int AUDIO_VOICE = 0;
const uint32_t AUDIO_CHUNK = 2048;
const int AUDIO_BUFFERS = 3;
const uint32_t RING_FRAMES = 65536;
const int MAX_SLOTS = 16;
const int SLOT_BUDGET = 6 * 1024 * 1024;
const double SEEK_STEP = 10.0;
const int UI_HIDE_FRAMES = 300;
const int VOLUME_STEP = 16;
const int TRACK_MARGIN = 40;
const int BTN_BOTTOM_CENTER = 44;

#define BARRIER() __asm__ volatile("" ::: "memory")

void formatVoteNumber(long num, char *out, size_t outSize)
{
	if(num < 0)
	{
		snprintf(out, outSize, "0");
		return;
	}
	char raw[32];
	snprintf(raw, sizeof(raw), "%ld", num);
	int len = strlen(raw);
	int outIdx = 0;
	for(int i = 0; i < len && outIdx < (int)outSize - 2; i++)
	{
		if(i > 0 && (len - i) % 3 == 0)
			out[outIdx++] = '.';
		out[outIdx++] = raw[i];
	}
	out[outIdx] = '\0';
}

struct UiButton
{
	GuiImage image;
	GuiImage imageOver;
	GuiImage icon;
	GuiButton button;

	UiButton(GuiImageData * bg, GuiImageData * bgOver, GuiImageData * glyph, int centerX, GuiTrigger * trigger) :
		image(bg),
		imageOver(bgOver),
		icon(glyph),
		button(bg->getWidth(), bg->getHeight())
	{
		icon.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
		button.setAlignment(ALIGN_H::CENTRE, ALIGN_V::BOTTOM);
		button.setPosition(centerX, -(BTN_BOTTOM_CENTER - bg->getHeight() / 2));
		button.setImage(&image);
		button.setImageOver(&imageOver);
		button.setIcon(&icon);
		button.setTrigger(trigger);
	}

	bool clicked()
	{
		if(button.getState() != STATE::CLICKED)
			return false;
		button.resetState();
		return true;
	}
};

class Player;
Player * active = nullptr;

class Player
{
	public:
		Player(Decoder * d, const MediaInfo & i, const char * p, const char * customTitle = nullptr, const YtResult * meta = nullptr) :
			dec(d), info(i), path(p), titleOverride(customTitle), ytMeta(meta) {}
		Player(const char * p, const char * customTitle = nullptr, const YtResult * meta = nullptr) :
			dec(nullptr), path(p), titleOverride(customTitle), ytMeta(meta) { memset(&info, 0, sizeof(info)); }
		~Player();
		PlayResult run(char * err, int errSize);
		void feedAudio(int voice);

	private:
		Decoder * dec;
		MediaInfo info;
		const char * path;
		const char * titleOverride;
		const YtResult * ytMeta;

		Mutex lock;
		Thread thread;
		volatile bool quit = false;
		volatile bool eof = false;
		volatile bool failed = false;
		volatile bool seekPending = false;
		volatile int wantFast = 0;
		double seekTarget = 0;
		char errorText[128] = "";

		YuvFrame frames[MAX_SLOTS];
		double framePts[MAX_SLOTS];
		bool busy[MAX_SLOTS] = { false };
		int fifo[MAX_SLOTS];
		int fifoHead = 0;
		int fifoCount = 0;
		int slotCount = 0;
		int shown = -1;
		double shownPts = 0;

		int16_t * ring = nullptr;
		int16_t * chunk[AUDIO_BUFFERS] = { nullptr };
		double chunkPts[AUDIO_BUFFERS] = { 0 };
		int playingChunk = 0;
		uint32_t ringBaseRd = 0;
		volatile uint32_t ringRd = 0;
		volatile uint32_t ringWr = 0;
		volatile int pendingFill = 0;
		volatile uint64_t lastCallback = 0;
		bool ringFresh = true;
		double ringBasePts = 0;
		int nextChunk = 0;
		bool audioRunning = false;
		int volume = 200;

		bool started = false;
		bool paused = false;
		double pausedClock = 0;
		double baseClock = 0;
		uint64_t wallStart = 0;
		double displayTime = 0;

		bool setup(char * err, int errSize);
		static void * decodeEntry(void * arg);
		void decodeLoop();
		void pushVideo(const DecFrame & f);
		void pushAudio(const DecFrame & f);
		void fillChunk(int index);
		void startAudio();
		void tryStart();
		void consumeVideo(double t);
		bool finished();
		double clock();
		void togglePause();
		void requestSeek(double t);
		void seekBy(double delta);
		void changeVolume(int delta);

		Thread pfpThread;
		Mutex pfpLock;
		void * pfpTexture = nullptr;
		int pfpWidth = 0;
		int pfpHeight = 0;
		volatile bool pfpReady = false;
		volatile bool pfpStop = false;

		static void * pfpEntry(void * arg);
		void pfpLoop();
};

void audioCallback(int voice)
{
	if(active)
		active->feedAudio(voice);
}

Player::~Player()
{
	active = nullptr;
	if(audioRunning)
	{
		ASND_StopVoice(AUDIO_VOICE);
		audioRunning = false;
	}

	quit = true;
	if(dec)
		decAbort(dec);
	if(thread.isRunning())
		thread.join();

	pfpStop = true;
	if(pfpThread.isRunning())
		pfpThread.join();

	if(pfpTexture)
	{
		platform->getVideo()->getImageRenderer()->destroyTexture(pfpTexture);
		pfpTexture = nullptr;
	}

	if(dec)
	{
		decClose(dec);
		dec = nullptr;
	}
	free(ring);
	ring = nullptr;
	for(int i = 0; i < AUDIO_BUFFERS; i++)
	{
		free(chunk[i]);
		chunk[i] = nullptr;
	}
	if(slotCount > 0)
	{
		yuvClose(frames, slotCount);
		slotCount = 0;
	}
}

bool Player::setup(char * err, int errSize)
{
	if(info.hasAudio)
	{
		if(info.sampleRate < 8000 || info.sampleRate > 48000)
		{
			snprintf(err, errSize, "Unsupported sample rate: %d Hz", info.sampleRate);
			return false;
		}

		ring = (int16_t *)memalign(32, RING_FRAMES * 4);
		for(int i = 0; i < AUDIO_BUFFERS; i++)
			chunk[i] = (int16_t *)memalign(32, AUDIO_CHUNK * 4);
		if(!ring || !chunk[0] || !chunk[1] || !chunk[2])
		{
			snprintf(err, errSize, "Out of memory");
			return false;
		}
	}

	if(info.hasVideo)
	{
		int slotBytes = info.width * info.height * 3 / 2;
		int count = SLOT_BUDGET / (slotBytes > 0 ? slotBytes : 1);
		count = count < 4 ? 4 : (count > MAX_SLOTS ? MAX_SLOTS : count);

		OgcVideoDriver * video = static_cast<OgcVideoDriver *>(platform->getVideo());
		bool wide = CONF_GetAspectRatio() == CONF_ASPECT_16_9;

		if(yuvOpen(video->getMode(), info.width, info.height, info.sarNum, info.sarDen, wide, frames, count) != 0)
		{
			snprintf(err, errSize, "Out of memory for video buffers");
			return false;
		}
		slotCount = count;
	}

	active = this;
	if(!thread.start(decodeEntry, this, 128 * 1024, ThreadPriority::Normal))
	{
		snprintf(err, errSize, "Could not start decoder thread");
		return false;
	}

	return true;
}

void * Player::pfpEntry(void * arg)
{
	static_cast<Player *>(arg)->pfpLoop();
	return nullptr;
}

void Player::pfpLoop()
{
	const char * pfpTarget = (ytMeta && ytMeta->avatarUrl[0]) ? ytMeta->avatarUrl : (ytMeta ? ytMeta->channelId : nullptr);
	if(!pfpTarget || pfpTarget[0] == '\0')
		return;

	int w = 0, h = 0;
	void * tex = ytFetchChannelPfp(pfpTarget, 48, 48, &w, &h);
	if(!tex)
		return;

	if(pfpStop || quit)
	{
		platform->getVideo()->getImageRenderer()->destroyTexture(tex);
		return;
	}

	pfpLock.lock();
	pfpTexture = tex;
	pfpWidth = w;
	pfpHeight = h;
	pfpReady = true;
	pfpLock.unlock();
}

void * Player::decodeEntry(void * arg)
{
	static_cast<Player *>(arg)->decodeLoop();
	return nullptr;
}

void Player::decodeLoop()
{
	DecFrame f;
	int fast = 0;

	while(!quit)
	{
		lock.lock();
		bool seek = seekPending;
		double target = seekTarget;
		lock.unlock();

		if(seek)
		{
			decSeek(dec, target);

			lock.lock();
			for(int i = 0; i < fifoCount; i++)
				busy[fifo[(fifoHead + i) % MAX_SLOTS]] = false;
			fifoCount = 0;
			ringRd = 0;
			ringWr = 0;
			ringFresh = true;
			eof = false;
			if(seekTarget == target)
				seekPending = false;
			lock.unlock();
			continue;
		}

		if(eof)
		{
			usleep(20000);
			continue;
		}

		if(wantFast != fast)
		{
			fast = wantFast;
			decSetFast(dec, fast);
		}

		DecResult r = decNext(dec, &f);

		if(r == DEC_VIDEO)
			pushVideo(f);
		else if(r == DEC_AUDIO)
			pushAudio(f);
		else
		{
			lock.lock();
			if(r == DEC_ERROR)
			{
				failed = true;
				snprintf(errorText, sizeof(errorText), "%s", decError(dec));
			}
			eof = true;
			lock.unlock();
		}
	}
}

void Player::pushVideo(const DecFrame & f)
{
	int slot = -1;

	lock.lock();
	while(!quit && !seekPending)
	{
		for(int i = 0; i < slotCount; i++)
		{
			if(!busy[i])
			{
				slot = i;
				break;
			}
		}
		if(slot >= 0)
			break;

		if(!started && fifoCount > 0)
		{
			slot = fifo[fifoHead];
			fifoHead = (fifoHead + 1) % MAX_SLOTS;
			fifoCount--;
			break;
		}

		lock.unlock();
		usleep(5000);
		lock.lock();
	}

	if(slot >= 0 && !quit && !seekPending)
		busy[slot] = true;
	else
		slot = -1;
	lock.unlock();

	if(slot < 0)
		return;

	yuvFill(&frames[slot], f.planes, f.strides);

	lock.lock();
	if(seekPending)
		busy[slot] = false;
	else
	{
		framePts[slot] = f.pts;
		fifo[(fifoHead + fifoCount) % MAX_SLOTS] = slot;
		fifoCount++;
	}
	lock.unlock();
}

void Player::pushAudio(const DecFrame & f)
{
	uint32_t n = f.frames;

	lock.lock();
	while(!quit && !seekPending && RING_FRAMES - (ringWr - ringRd) < n)
	{
		lock.unlock();
		usleep(5000);
		lock.lock();
	}

	if(quit || seekPending)
	{
		lock.unlock();
		return;
	}

	double expectedPts = ringBasePts + (double)(ringWr - ringBaseRd) / info.sampleRate;
	if(ringFresh || ringWr == ringRd || fabs(f.pts - expectedPts) > 0.05)
	{
		ringBasePts = f.pts;
		ringBaseRd = ringWr;
		ringFresh = false;
	}

	uint32_t pos = ringWr & (RING_FRAMES - 1);
	uint32_t first = RING_FRAMES - pos < n ? RING_FRAMES - pos : n;
	memcpy(ring + pos * 2, f.pcm, first * 4);
	memcpy(ring, f.pcm + first * 2, (n - first) * 4);
	BARRIER();
	ringWr = ringWr + n;
	lock.unlock();
}

void Player::fillChunk(int index)
{
	int16_t * buf = chunk[index];
	lock.lock();
	uint32_t avail = ringWr - ringRd;
	uint32_t n = avail < AUDIO_CHUNK ? avail : AUDIO_CHUNK;
	uint32_t pos = ringRd & (RING_FRAMES - 1);
	uint32_t first = RING_FRAMES - pos < n ? RING_FRAMES - pos : n;

	memcpy(buf, ring + pos * 2, first * 4);
	memcpy(buf + first * 2, ring, (n - first) * 4);
	memset(buf + n * 2, 0, (AUDIO_CHUNK - n) * 4);
	BARRIER();
	chunkPts[index] = ringBasePts + (double)(ringRd - ringBaseRd) / info.sampleRate;
	ringRd = ringRd + n;
	lock.unlock();
	DCFlushRange(buf, AUDIO_CHUNK * 4);
}

void Player::feedAudio(int voice)
{
	playingChunk = (playingChunk + 1) % AUDIO_BUFFERS;
	lastCallback = gettime();
	ASND_AddVoice(voice, chunk[nextChunk], AUDIO_CHUNK * 4);
	nextChunk = (nextChunk + 1) % AUDIO_BUFFERS;
	pendingFill++;
}

void Player::startAudio()
{
	fillChunk(0);
	fillChunk(1);
	fillChunk(2);
	playingChunk = 0;
	nextChunk = 2;
	pendingFill = 0;
	lastCallback = gettime();
	ASND_SetVoice(AUDIO_VOICE, VOICE_STEREO_16BIT, info.sampleRate, 0, chunk[0], AUDIO_CHUNK * 4, volume, volume, audioCallback);
	ASND_AddVoice(AUDIO_VOICE, chunk[1], AUDIO_CHUNK * 4);
	audioRunning = true;
}

void Player::tryStart()
{
	lock.lock();
	uint32_t avail = ringWr - ringRd;
	int need = slotCount < 4 ? slotCount - 1 : 3;
	bool ready = !seekPending;
	double firstPts = fifoCount > 0 ? framePts[fifo[fifoHead]] : 0;

	if(ready && info.hasAudio)
		ready = avail >= AUDIO_CHUNK * 6 || eof;
	if(ready && info.hasVideo)
		ready = fifoCount >= need || eof;
	lock.unlock();

	if(!ready)
		return;

	if(info.hasAudio)
		startAudio();
	else
	{
		baseClock = firstPts;
		wallStart = gettime();
	}

	started = true;
}

double Player::clock()
{
	if(paused)
		return pausedClock;

	if(info.hasAudio && audioRunning)
	{
		uint64_t last;
		int curChunk;
		do {
			last = lastCallback;
			curChunk = playingChunk;
		} while(last != lastCallback);

		double chunkSecs = (double)AUDIO_CHUNK / info.sampleRate;
		double elapsed = ticks_to_millisecs(gettime() - last) / 1000.0;
		if(elapsed > chunkSecs)
			elapsed = chunkSecs;
		return chunkPts[curChunk] + elapsed;
	}

	return baseClock + ticks_to_millisecs(gettime() - wallStart) / 1000.0;
}

void Player::consumeVideo(double t)
{
	lock.lock();
	while(fifoCount > 0 && framePts[fifo[fifoHead]] <= t)
	{
		if(shown >= 0)
			busy[shown] = false;
		shown = fifo[fifoHead];
		shownPts = framePts[shown];
		fifoHead = (fifoHead + 1) % MAX_SLOTS;
		fifoCount--;
	}

	if(fifoCount == 0 && !eof && t - shownPts > 0.3)
		wantFast = 1;
	else if(fifoCount >= slotCount / 2)
		wantFast = 0;
	lock.unlock();
}

bool Player::finished()
{
	lock.lock();
	bool done = started && eof && fifoCount == 0 && (!info.hasAudio || ringRd == ringWr);
	lock.unlock();
	return done;
}

void Player::togglePause()
{
	if(!started)
		return;

	if(!paused)
	{
		pausedClock = clock();
		paused = true;
		if(info.hasAudio)
			ASND_PauseVoice(AUDIO_VOICE, 1);
	}
	else
	{
		paused = false;
		if(info.hasAudio)
		{
			lastCallback = gettime();
			ASND_PauseVoice(AUDIO_VOICE, 0);
		}
		else
		{
			baseClock = pausedClock;
			wallStart = gettime();
		}
	}
}

void Player::requestSeek(double t)
{
	if(audioRunning)
	{
		ASND_StopVoice(AUDIO_VOICE);
		audioRunning = false;
		pendingFill = 0;
	}

	lock.lock();
	seekTarget = t;
	seekPending = true;
	lock.unlock();

	started = false;
	paused = false;
	displayTime = t;
}

void Player::seekBy(double delta)
{
	double t = (started ? clock() : displayTime) + delta;
	double limit = info.duration > 1 ? info.duration - 1 : 0;

	if(t < 0)
		t = 0;
	if(limit > 0 && t > limit)
		t = limit;
	requestSeek(t);
}

void Player::changeVolume(int delta)
{
	volume += delta;
	volume = volume < 0 ? 0 : (volume > 255 ? 255 : volume);
	if(audioRunning)
		ASND_ChangeVolumeVoice(AUDIO_VOICE, volume, volume);
}

PlayResult Player::run(char * err, int errSize)
{
	struct OpenTask
	{
		const char * path;
		MediaInfo info;
		char err[128];
		Decoder * dec = nullptr;
		volatile bool done = false;
	} task;

	task.path = path;
	task.err[0] = '\0';
	task.done = false;

	Thread openThread;
	bool opening = false;

	if(!dec)
	{
		opening = true;
		openThread.start([](void * arg) -> void * {
			OpenTask * t = static_cast<OpenTask *>(arg);
			t->dec = decOpen(t->path, &t->info, t->err, sizeof(t->err));
			t->done = true;
			return nullptr;
		}, &task, 128 * 1024, ThreadPriority::Normal);
	}
	else
	{
		if(!setup(err, errSize))
			return PLAY_ERROR;
	}

	const PixelColor white = {255, 255, 255, 255};
	const PixelColor accent = {0, 120, 215, 255};
	const char * name = strrchr(path, '/');
	name = name ? name + 1 : path;
	const char * displayTitle = (titleOverride && titleOverride[0]) ? titleOverride : (info.title[0] ? info.title : name);

	GuiTrigger trigA;
	trigA.setPrimaryTrigger();

	VideoDriver * video = platform->getVideo();
	int screenWidth = video->getScreenWidth();
	int screenHeight = video->getScreenHeight();
	int trackWidth = screenWidth - TRACK_MARGIN * 2;

	GuiWindow ui(screenWidth, screenHeight);

	GuiImage background(screenWidth, screenHeight, (PixelColor){18, 20, 28, 255});
	GuiImage accentBar(240, 3, accent);
	GuiImage topBg(screenWidth, ytMeta ? 70 : 56, (PixelColor){0, 0, 0, 255});
	topBg.setAlpha(170);

	pfpTexture = nullptr;
	pfpWidth = 0;
	pfpHeight = 0;
	pfpReady = false;
	pfpStop = false;

	if(ytMeta && (ytMeta->avatarUrl[0] || ytMeta->channelId[0]))
		pfpThread.start(pfpEntry, this, 32 * 1024, ThreadPriority::Normal);

	GuiImageData * pfpData = nullptr;
	GuiImage * pfpImg = nullptr;
	bool pfpApplied = false;
	int textX = ytMeta ? 72 : 40;

	GuiText nameTxt(displayTitle, ytMeta ? 20 : 24, white);
	nameTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	nameTxt.setPosition(textX, ytMeta ? 12 : 15);
	nameTxt.setMaxWidth(screenWidth - textX - 20);

	GuiText chanTxt((ytMeta && ytMeta->author[0]) ? ytMeta->author : "", 15, (PixelColor){220, 225, 235, 255});
	chanTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	chanTxt.setPosition(textX, 39);

	int chanW = chanTxt.getTextWidth();
	GuiButton chanBtn(chanW > 0 ? chanW + 16 : 0, 24);
	chanBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	chanBtn.setPosition(textX - 4, 37);
	chanBtn.setTrigger(&trigA);

	GuiButton pfpBtn(50, 50);
	pfpBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	pfpBtn.setPosition(14, 10);
	pfpBtn.setTrigger(&trigA);

	int dateX = textX + chanW + (chanW > 0 ? 14 : 0);
	GuiText dateTxt((ytMeta && ytMeta->publishedText[0]) ? ytMeta->publishedText : "", 14, (PixelColor){160, 165, 180, 255});
	dateTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	dateTxt.setPosition(dateX, 40);

	int dateW = dateTxt.getTextWidth();
	int viewsX = dateX + dateW + (dateW > 0 ? 14 : 0);
	GuiText viewsTxt((ytMeta && ytMeta->viewCountText[0]) ? ytMeta->viewCountText : "", 14, (PixelColor){160, 165, 180, 255});
	viewsTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	viewsTxt.setPosition(viewsX, 40);

	GuiImage knob(12, 20, white);
	knob.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	knob.setPosition(TRACK_MARGIN - 6, -115);
	GuiText titleTxt(displayTitle, 30, white);
	GuiText artistTxt(info.artist, 22, white);
	GuiText albumTxt(info.album, 22, white);
	titleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	titleTxt.setPosition(0, -90);
	titleTxt.setMaxWidth(600);
	artistTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	artistTxt.setPosition(0, -35);
	accentBar.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	accentBar.setPosition(0, -62);
	albumTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	albumTxt.setPosition(0, 0);

	GuiImage barBg(screenWidth, 148, (PixelColor){10, 12, 18, 255});
	barBg.setAlpha(215);
	barBg.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);

	GuiImage trackBg(trackWidth, 8, (PixelColor){70, 74, 90, 255});
	trackBg.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	trackBg.setPosition(TRACK_MARGIN, -121);

	GuiImage trackFill(1, 8, accent);
	trackFill.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	trackFill.setPosition(TRACK_MARGIN, -121);

	GuiButton seekBtn(trackWidth, 24);
	seekBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	seekBtn.setPosition(TRACK_MARGIN, -113);
	seekBtn.setTrigger(&trigA);

	GuiText timeTxt("0:00 / 0:00", 20, white);
	timeTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	timeTxt.setPosition(40, -84);

	GuiText volTxt("", 20, white);
	volTxt.setAlignment(ALIGN_H::RIGHT, ALIGN_V::BOTTOM);
	volTxt.setPosition(-40, -84);

	GuiImageData btnSmall(player_btn_png);
	GuiImageData btnSmallOver(player_btn_over_png);
	GuiImageData btnBig(player_btn_big_png);
	GuiImageData btnBigOver(player_btn_big_over_png);
	GuiImageData iconPlay(icon_play_png);
	GuiImageData iconPause(icon_pause_png);
	GuiImageData iconStop(icon_stop_png);
	GuiImageData iconRewind(icon_rewind_png);
	GuiImageData iconForward(icon_forward_png);
	GuiImageData iconVolDown(icon_vol_down_png);
	GuiImageData iconVolUp(icon_vol_up_png);
	GuiImageData iconMenu(icon_menu_png);

	UiButton stopBtn(&btnSmall, &btnSmallOver, &iconStop, -250, &trigA);
	UiButton menuBtn(&btnSmall, &btnSmallOver, &iconMenu, -320, &trigA);
	UiButton backBtn(&btnSmall, &btnSmallOver, &iconRewind, -80, &trigA);
	UiButton playBtn(&btnBig, &btnBigOver, &iconPause, 0, &trigA);
	UiButton fwdBtn(&btnSmall, &btnSmallOver, &iconForward, 80, &trigA);
	UiButton volDownBtn(&btnSmall, &btnSmallOver, &iconVolDown, 200, &trigA);
	UiButton volUpBtn(&btnSmall, &btnSmallOver, &iconVolUp, 260, &trigA);

	GuiText loadingTxt("Loading...", 24, white);
	loadingTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	loadingTxt.setPosition(0, -20);

	ui.append(&background);
	if(ytMeta || !dec || info.hasVideo)
	{
		ui.append(&topBg);
		ui.append(&nameTxt);
		if(ytMeta)
		{
			if(ytMeta->author[0])
			{
				ui.append(&chanTxt);
				ui.append(&chanBtn);
				ui.append(&pfpBtn);
			}
			if(ytMeta->publishedText[0]) ui.append(&dateTxt);
			if(ytMeta->viewCountText[0]) ui.append(&viewsTxt);
		}
	}
	else
	{
		ui.append(&titleTxt);
		ui.append(&artistTxt);
		ui.append(&albumTxt);
		ui.append(&accentBar);
	}
	ui.append(&barBg);
	ui.append(&trackBg);
	ui.append(&trackFill);
	ui.append(&knob);
	ui.append(&seekBtn);
	ui.append(&timeTxt);
	ui.append(&volTxt);
	ui.append(&stopBtn.button);
	if(ytMeta) ui.append(&menuBtn.button);
	ui.append(&backBtn.button);
	ui.append(&playBtn.button);
	ui.append(&fwdBtn.button);
	ui.append(&volDownBtn.button);
	ui.append(&volUpBtn.button);
	ui.append(&loadingTxt);

	enum class Overlay { NONE, MENU, INFO, SUGGESTIONS };
	Overlay overlayState = Overlay::NONE;

	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	GuiSound btnSoundOver2(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	const PixelColor btnTextCol = {25, 28, 38, 255};
	const PixelColor dimCol = {0, 0, 0, 160};

	GuiImage overlayDim(screenWidth, screenHeight, dimCol);

	int menuW = 320, menuH = 246;
	GuiWindow menuWin(menuW, menuH);
	menuWin.setPosition((screenWidth - menuW) / 2, (screenHeight - menuH) / 2);
	menuWin.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	GuiImage menuBg(320, 246, (PixelColor){22, 25, 36, 250});
	GuiImage menuBorder(320, 3, accent);
	menuBorder.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	GuiText menuTitleTxt("Video Menu", 20, white);
	menuTitleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	menuTitleTxt.setPosition(0, 14);

	GuiImage infoOptImg(&btnOutline);
	infoOptImg.setSize(260, 36);
	GuiImage infoOptImgOver(&btnOutlineOver);
	infoOptImgOver.setSize(260, 36);
	GuiText infoOptTxt("Video Info", 17, btnTextCol);
	GuiButton infoOptBtn(260, 36);
	infoOptBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	infoOptBtn.setPosition(0, 48);
	infoOptBtn.setImage(&infoOptImg);
	infoOptBtn.setImageOver(&infoOptImgOver);
	infoOptBtn.setLabel(&infoOptTxt);
	infoOptBtn.setSoundOver(&btnSoundOver2);
	infoOptBtn.setTrigger(&trigA);
	infoOptBtn.setEffectGrow();

	GuiImage suggOptImg(&btnOutline);
	suggOptImg.setSize(260, 36);
	GuiImage suggOptImgOver(&btnOutlineOver);
	suggOptImgOver.setSize(260, 36);
	GuiText suggOptTxt("Suggestions", 17, btnTextCol);
	GuiButton suggOptBtn(260, 36);
	suggOptBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	suggOptBtn.setPosition(0, 92);
	suggOptBtn.setImage(&suggOptImg);
	suggOptBtn.setImageOver(&suggOptImgOver);
	suggOptBtn.setLabel(&suggOptTxt);
	suggOptBtn.setSoundOver(&btnSoundOver2);
	suggOptBtn.setTrigger(&trigA);
	suggOptBtn.setEffectGrow();

	YtCaptionTrackList captionTracks;
	captionTracks.count = 0;
	ytGetCaptionTracks(&captionTracks);

	char captionsOptStr[64];
	if(captionTracks.count == 0)
		snprintf(captionsOptStr, sizeof(captionsOptStr), "Captions: None");
	else
		snprintf(captionsOptStr, sizeof(captionsOptStr), "Captions: Off");
	GuiText captionsOptTxt(captionsOptStr, 17, btnTextCol);
	GuiImage captionsOptImg(&btnOutline);
	captionsOptImg.setSize(260, 36);
	GuiImage captionsOptImgOver(&btnOutlineOver);
	captionsOptImgOver.setSize(260, 36);
	GuiButton captionsOptBtn(260, 36);
	captionsOptBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	captionsOptBtn.setPosition(0, 136);
	captionsOptBtn.setImage(&captionsOptImg);
	captionsOptBtn.setImageOver(&captionsOptImgOver);
	captionsOptBtn.setLabel(&captionsOptTxt);
	captionsOptBtn.setSoundOver(&btnSoundOver2);
	captionsOptBtn.setTrigger(&trigA);
	captionsOptBtn.setEffectGrow();

	int selectedCaptionTrack = -1;
	bool captionsActive = false;
	std::vector<YtCaptionLine> activeCaptions;
	std::vector<YtCaptionLine> cachedCaptions[YT_MAX_CAPTION_TRACKS];
	bool trackCached[YT_MAX_CAPTION_TRACKS] = { false };
	Thread capThread;
	struct CapTask
	{
		char url[1024];
		std::vector<YtCaptionLine> lines;
		volatile bool done = false;
		volatile bool success = false;
	} capTask;

	GuiImage menuCloseImg(&btnOutline);
	menuCloseImg.setSize(140, 34);
	GuiImage menuCloseImgOver(&btnOutlineOver);
	menuCloseImgOver.setSize(140, 34);
	GuiText menuCloseTxt("Close", 18, btnTextCol);
	GuiButton menuCloseBtn(140, 34);
	menuCloseBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	menuCloseBtn.setPosition(0, 192);
	menuCloseBtn.setImage(&menuCloseImg);
	menuCloseBtn.setImageOver(&menuCloseImgOver);
	menuCloseBtn.setLabel(&menuCloseTxt);
	menuCloseBtn.setSoundOver(&btnSoundOver2);
	menuCloseBtn.setTrigger(&trigA);
	menuCloseBtn.setEffectGrow();

	menuWin.append(&menuBg);
	menuWin.append(&menuBorder);
	menuWin.append(&menuTitleTxt);
	menuWin.append(&infoOptBtn);
	menuWin.append(&suggOptBtn);
	menuWin.append(&captionsOptBtn);
	menuWin.append(&menuCloseBtn);

	char lastCapLine[256] = "";
	struct CaptionDisplay
	{
		char lines[4][128];
		uint16_t lineWidths[4];
		int lineCount = 0;
		uint16_t maxW = 0;
		int totalH = 0;
	} capDisp;

	int infoW = 560, infoH = 400;
	GuiWindow infoWin(infoW, infoH);
	infoWin.setPosition((screenWidth - infoW) / 2, (screenHeight - infoH) / 2);
	infoWin.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	GuiImage infoBg(560, 400, (PixelColor){22, 25, 36, 250});
	GuiImage infoBorder(560, 3, accent);
	infoBorder.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	GuiText infoTitleTxt("Video Info", 22, white);
	infoTitleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	infoTitleTxt.setPosition(0, 12);

	char metaRow1Str[160] = "";
	GuiText metaRow1Txt(metaRow1Str, 15, white);
	metaRow1Txt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	metaRow1Txt.setPosition(24, 44);
	metaRow1Txt.setMaxWidth(512);

	char metaRow2Str[160] = "";
	GuiText metaRow2Txt(metaRow2Str, 14, (PixelColor){170, 180, 205, 255});
	metaRow2Txt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	metaRow2Txt.setPosition(24, 68);
	metaRow2Txt.setMaxWidth(512);

	char votesStr[128] = "Likes: ...   •   Dislikes: ...";
	GuiText votesTxt(votesStr, 15, (PixelColor){170, 225, 175, 255});
	votesTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	votesTxt.setPosition(24, 92);
	votesTxt.setMaxWidth(512);

	GuiImage infoSep(512, 1, (PixelColor){50, 56, 75, 255});
	infoSep.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	infoSep.setPosition(0, 118);

	GuiText descHeaderTxt("Description:", 14, (PixelColor){145, 155, 175, 255});
	descHeaderTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	descHeaderTxt.setPosition(24, 126);

	char descBuf[384] = "";
	GuiText descTxt(descBuf, 14, (PixelColor){215, 220, 230, 255});
	descTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	descTxt.setPosition(24, 148);
	descTxt.setWrap(true, 512);

	GuiImage infoCloseImg(&btnOutline);
	infoCloseImg.setSize(140, 34);
	GuiImage infoCloseImgOver(&btnOutlineOver);
	infoCloseImgOver.setSize(140, 34);
	GuiText infoCloseTxt("Close", 18, btnTextCol);
	GuiButton infoCloseBtn(140, 34);
	infoCloseBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::BOTTOM);
	infoCloseBtn.setPosition(0, -14);
	infoCloseBtn.setImage(&infoCloseImg);
	infoCloseBtn.setImageOver(&infoCloseImgOver);
	infoCloseBtn.setLabel(&infoCloseTxt);
	infoCloseBtn.setSoundOver(&btnSoundOver2);
	infoCloseBtn.setTrigger(&trigA);
	infoCloseBtn.setEffectGrow();

	infoWin.append(&infoBg);
	infoWin.append(&infoBorder);
	infoWin.append(&infoTitleTxt);
	infoWin.append(&metaRow1Txt);
	infoWin.append(&metaRow2Txt);
	infoWin.append(&votesTxt);
	infoWin.append(&infoSep);
	infoWin.append(&descHeaderTxt);
	infoWin.append(&descTxt);
	infoWin.append(&infoCloseBtn);

	bool votesRequested = false;
	Thread voteThread;
	struct VoteTask
	{
		char videoId[16];
		YtVoteData vote;
		char err[128];
		bool success = false;
		volatile bool done = false;
	} voteTask;

	int suggW = 560, suggH = 400;
	GuiWindow suggWin(suggW, suggH);
	suggWin.setPosition((screenWidth - suggW) / 2, (screenHeight - suggH) / 2);
	suggWin.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	GuiImage suggBg(560, 400, (PixelColor){22, 25, 36, 250});
	GuiImage suggBorder(560, 3, accent);
	suggBorder.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	GuiText suggTitleTxt("Suggestions", 22, white);
	suggTitleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	suggTitleTxt.setPosition(0, 14);

	GuiText suggStatusTxt("Loading suggestions...", 16, (PixelColor){180, 185, 200, 255});
	suggStatusTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	suggStatusTxt.setPosition(0, 52);

	GuiImage suggCloseImg(&btnOutline);
	suggCloseImg.setSize(140, 34);
	GuiImage suggCloseImgOver(&btnOutlineOver);
	suggCloseImgOver.setSize(140, 34);
	GuiText suggCloseTxt("Close", 18, btnTextCol);
	GuiButton suggCloseBtn(140, 34);
	suggCloseBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::BOTTOM);
	suggCloseBtn.setPosition(0, -14);
	suggCloseBtn.setImage(&suggCloseImg);
	suggCloseBtn.setImageOver(&suggCloseImgOver);
	suggCloseBtn.setLabel(&suggCloseTxt);
	suggCloseBtn.setSoundOver(&btnSoundOver2);
	suggCloseBtn.setTrigger(&trigA);
	suggCloseBtn.setEffectGrow();

	suggWin.append(&suggBg);
	suggWin.append(&suggBorder);
	suggWin.append(&suggTitleTxt);
	suggWin.append(&suggStatusTxt);
	suggWin.append(&suggCloseBtn);

	const int MAX_SUGGESTIONS = 5;
	bool suggRequested = false;
	Thread suggThread;
	struct SuggestTask
	{
		char videoId[16];
		YtResult results[MAX_SUGGESTIONS];
		void * textures[MAX_SUGGESTIONS];
		int widths[MAX_SUGGESTIONS];
		int heights[MAX_SUGGESTIONS];
		volatile bool thumbReady[MAX_SUGGESTIONS];
		char err[128];
		int count = 0;
		volatile bool suggestionsReady = false;
		volatile bool stop = false;
		volatile bool done = false;
	} suggTask;

	GuiButton * suggRowBtn[MAX_SUGGESTIONS] = { nullptr };
	GuiText * suggRowTxt[MAX_SUGGESTIONS] = { nullptr };
	GuiText * suggSubTxt[MAX_SUGGESTIONS] = { nullptr };
	GuiImage * suggRowBg[MAX_SUGGESTIONS] = { nullptr };
	GuiImage * suggRowBgOver[MAX_SUGGESTIONS] = { nullptr };
	GuiImageData * suggThumbData[MAX_SUGGESTIONS] = { nullptr };
	GuiImage * suggThumbImg[MAX_SUGGESTIONS] = { nullptr };
	char suggRowStr[MAX_SUGGESTIONS][192];
	char suggSubStr[MAX_SUGGESTIONS][192];
	bool suggRowsBuilt = false;

	OgcVideoDriver * ogcVideo = static_cast<OgcVideoDriver *>(video);
	float lastX[4] = { -1, -1, -1, -1 };
	float lastY[4] = { -1, -1, -1, -1 };
	int idle = 0;
	int lastSecond = -1;
	int lastVolume = -1;
	bool lastPaused = false;
	int scrubChan = -1;
	double scrubTarget = 0;
	int loadAnim = 0;
	bool loadingUiActive = true;
	PlayResult result = PLAY_DONE;

	for(;;)
	{
		if(platform->shouldExit())
		{
			result = PLAY_EXIT;
			break;
		}

		platform->getInput()->update();

		uint32_t pressed = 0;
		bool moved = false;
		for(int i = 0; i < 4; i++)
		{
			const InputPadData & pad = controller[i]->getPadData();
			pressed |= pad.buttons_d;
			if(pad.validPointer)
			{
				if(fabsf(pad.cursor_x - lastX[i]) + fabsf(pad.cursor_y - lastY[i]) > 3)
					moved = true;
				lastX[i] = pad.cursor_x;
				lastY[i] = pad.cursor_y;
			}
		}

		idle = (pressed || moved) ? 0 : idle + 1;
		bool uiVisible = !started || !info.hasVideo || paused || idle < UI_HIDE_FRAMES || overlayState != Overlay::NONE;

		if(pressed & (INPUT_BTN_B | INPUT_BTN_1))
		{
			if(overlayState != Overlay::NONE)
			{
				overlayState = Overlay::NONE;
			}
			else
			{
				if(opening && task.dec)
					decAbort(task.dec);
				else if(dec)
					decAbort(dec);
				break;
			}
		}
		if(overlayState == Overlay::NONE)
		{
			if(pressed & (INPUT_BTN_PLUS | INPUT_BTN_2))
				togglePause();
			if(pressed & INPUT_BTN_RIGHT)
				seekBy(SEEK_STEP);
			if(pressed & INPUT_BTN_LEFT)
				seekBy(-SEEK_STEP);
			if(pressed & INPUT_BTN_UP)
				changeVolume(VOLUME_STEP);
			if(pressed & INPUT_BTN_DOWN)
				changeVolume(-VOLUME_STEP);
		}

		if(opening)
		{
			if(task.done)
			{
				openThread.join();
				opening = false;

				if(!task.dec)
				{
					snprintf(err, errSize, "%s", task.err[0] ? task.err : "Could not open stream");
					result = PLAY_ERROR;
					break;
				}

				dec = task.dec;
				info = task.info;

				if(!setup(err, errSize))
				{
					result = PLAY_ERROR;
					break;
				}

				if(!info.hasVideo && !ytMeta)
				{
					ui.remove(&topBg);
					ui.remove(&nameTxt);
					ui.append(&titleTxt);
					ui.append(&artistTxt);
					ui.append(&albumTxt);
					ui.append(&accentBar);
				}

				if((!titleOverride || !titleOverride[0]) && info.title[0])
				{
					nameTxt.setText(info.title);
					titleTxt.setText(info.title);
				}
				if(info.artist[0]) artistTxt.setText(info.artist);
				if(info.album[0]) albumTxt.setText(info.album);
			}
		}

		if(!pfpApplied && pfpReady)
		{
			pfpLock.lock();
			void * tex = pfpTexture;
			int w = pfpWidth;
			int h = pfpHeight;
			pfpTexture = nullptr;
			pfpLock.unlock();

			if(tex)
			{
				pfpData = new GuiImageData(tex, w, h, true);
				pfpImg = new GuiImage(pfpData);
				pfpImg->setSize(46, 46);
				pfpImg->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
				pfpImg->setPosition(16, 12);
				ui.append(pfpImg);
			}
			pfpApplied = true;
		}

		if(uiVisible)
		{
			for(int i = 3; i >= 0; i--)
				ui.update(controller[i]);

			if(overlayState == Overlay::NONE)
			{
				if(backBtn.clicked())
					seekBy(-SEEK_STEP);
				if(playBtn.clicked())
					togglePause();
				if(fwdBtn.clicked())
					seekBy(SEEK_STEP);
				if(volDownBtn.clicked())
					changeVolume(-VOLUME_STEP);
				if(volUpBtn.clicked())
					changeVolume(VOLUME_STEP);
				if(stopBtn.clicked())
				{
					if(opening && task.dec)
						decAbort(task.dec);
					else if(dec)
						decAbort(dec);
					break;
				}

				if(ytMeta && ytMeta->author[0] && (chanBtn.getState() == STATE::CLICKED || pfpBtn.getState() == STATE::CLICKED))
				{
					chanBtn.resetState();
					pfpBtn.resetState();
					SetNextChannel(ytMeta->channelId[0] ? ytMeta->channelId : ytMeta->author, ytMeta->author, ytMeta->avatarUrl);
					if(opening && task.dec)
						decAbort(task.dec);
					else if(dec)
						decAbort(dec);
					result = PLAY_CHANNEL;
					break;
				}

				if(menuBtn.clicked())
				{
					overlayState = Overlay::MENU;
				}

				if(seekBtn.getState() == STATE::CLICKED && scrubChan < 0)
				{
					seekBtn.resetState();
					int ch = seekBtn.getStateChan();
					if(ch >= 0 && ch < 4 && info.duration > 0)
						scrubChan = ch;
				}
			}
			else if(overlayState == Overlay::MENU)
			{
				for(int i = 3; i >= 0; i--) menuWin.update(controller[i]);

				if(infoOptBtn.getState() == STATE::CLICKED)
				{
					infoOptBtn.resetState();

					const char *chan = (ytMeta && ytMeta->author[0]) ? ytMeta->author : "Unknown";
					char dur[32] = "";
					if(ytMeta && ytMeta->lengthText[0])
						snprintf(dur, sizeof(dur), "%s", ytMeta->lengthText);
					else if(info.duration > 0)
					{
						int s = (int)info.duration;
						int h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
						if(h > 0) snprintf(dur, sizeof(dur), "%d:%02d:%02d", h, m, sec);
						else snprintf(dur, sizeof(dur), "%02d:%02d", m, sec);
					}
					else
					{
						snprintf(dur, sizeof(dur), "Unknown");
					}
					snprintf(metaRow1Str, sizeof(metaRow1Str), "Channel: %s   •   Duration: %s", chan, dur);
					metaRow1Txt.setText(metaRow1Str);

					const char *views = (ytMeta && ytMeta->viewCountText[0]) ? ytMeta->viewCountText : "N/A";
					const char *pub = (ytMeta && ytMeta->publishedText[0]) ? ytMeta->publishedText : "N/A";
					snprintf(metaRow2Str, sizeof(metaRow2Str), "Views: %s   •   Uploaded: %s", views, pub);
					metaRow2Txt.setText(metaRow2Str);

					const char *rawDesc = (ytMeta && ytMeta->description[0]) ? ytMeta->description : nullptr;
					if(rawDesc && rawDesc[0] != '\0')
					{
						int dIdx = 0;
						bool prevSpace = false;
						for(int i = 0; rawDesc[i] != '\0' && dIdx < (int)sizeof(descBuf) - 5; i++)
						{
							char ch = rawDesc[i];
							if(ch == '\r') continue;
							if(ch == '\n' || ch == '\t') ch = ' ';
							if(ch == ' ')
							{
								if(prevSpace) continue;
								prevSpace = true;
							}
							else
							{
								prevSpace = false;
							}
							descBuf[dIdx++] = ch;
						}
						if(strlen(rawDesc) > (size_t)dIdx)
						{
							descBuf[dIdx++] = '.';
							descBuf[dIdx++] = '.';
							descBuf[dIdx++] = '.';
						}
						descBuf[dIdx] = '\0';
					}
					else
					{
						snprintf(descBuf, sizeof(descBuf), "No description available.");
					}
					descTxt.setText(descBuf);

					if(!votesRequested)
					{
						votesRequested = true;
						votesTxt.setText("Likes: ...   •   Dislikes: ...");
						snprintf(voteTask.videoId, sizeof(voteTask.videoId), "%s", ytMeta ? ytMeta->videoId : "");
						voteTask.err[0] = '\0';
						voteTask.success = false;
						voteTask.done = false;
						voteThread.start([](void * arg) -> void * {
							VoteTask * t = static_cast<VoteTask *>(arg);
							t->success = ytFetchVoteData(t->videoId, &t->vote, t->err, sizeof(t->err));
							t->done = true;
							return nullptr;
						}, &voteTask, 32 * 1024, ThreadPriority::Normal);
					}
					overlayState = Overlay::INFO;
				}
				else if(suggOptBtn.getState() == STATE::CLICKED)
				{
					suggOptBtn.resetState();
					if(!suggRequested)
					{
						suggRequested = true;
						snprintf(suggTask.videoId, sizeof(suggTask.videoId), "%s", ytMeta ? ytMeta->videoId : "");
						suggTask.err[0] = '\0';
						suggTask.count = 0;
						suggTask.suggestionsReady = false;
						suggTask.stop = false;
						suggTask.done = false;
						for(int i = 0; i < MAX_SUGGESTIONS; i++)
						{
							suggTask.textures[i] = nullptr;
							suggTask.thumbReady[i] = false;
						}
						suggThread.start([](void * arg) -> void * {
							SuggestTask * t = static_cast<SuggestTask *>(arg);
							t->count = ytGetSuggestions(t->videoId, t->results, MAX_SUGGESTIONS, t->err, sizeof(t->err));
							t->suggestionsReady = true;
							for(int i = 0; i < t->count && !t->stop; i++)
							{
								int w = 0, h = 0;
								void * tex = ytFetchThumbnail(t->results[i].videoId, 72, 44, &w, &h);
								if(t->stop)
								{
									if(tex) platform->getVideo()->getImageRenderer()->destroyTexture(tex);
									break;
								}
								if(tex)
								{
									t->textures[i] = tex;
									t->widths[i] = w;
									t->heights[i] = h;
									t->thumbReady[i] = true;
								}
							}
							t->done = true;
							return nullptr;
						}, &suggTask, 64 * 1024, ThreadPriority::Normal);
					}
					overlayState = Overlay::SUGGESTIONS;
				}
				else if(captionsOptBtn.getState() == STATE::CLICKED)
				{
					captionsOptBtn.resetState();
					if(captionTracks.count > 0)
					{
						selectedCaptionTrack++;
						if(selectedCaptionTrack >= captionTracks.count)
							selectedCaptionTrack = -1;

						if(selectedCaptionTrack < 0)
						{
							captionsActive = false;
							activeCaptions.clear();
							snprintf(captionsOptStr, sizeof(captionsOptStr), "Captions: Off");
							captionsOptTxt.setText(captionsOptStr);
						}
						else
						{
							captionsActive = true;
							snprintf(captionsOptStr, sizeof(captionsOptStr), "Captions: %s", captionTracks.tracks[selectedCaptionTrack].name);
							captionsOptTxt.setText(captionsOptStr);

							if(trackCached[selectedCaptionTrack])
							{
								activeCaptions = cachedCaptions[selectedCaptionTrack];
							}
							else
							{
								if(capThread.isRunning())
									capThread.join();
								snprintf(capTask.url, sizeof(capTask.url), "%s", captionTracks.tracks[selectedCaptionTrack].baseUrl);
								capTask.lines.clear();
								capTask.done = false;
								capTask.success = false;
								capThread.start([](void * arg) -> void * {
									CapTask * t = static_cast<CapTask *>(arg);
									t->success = ytFetchCaptions(t->url, t->lines);
									t->done = true;
									return nullptr;
								}, &capTask, 64 * 1024, ThreadPriority::Normal);
							}
						}
					}
				}
				else if(menuCloseBtn.getState() == STATE::CLICKED)
				{
					menuCloseBtn.resetState();
					overlayState = Overlay::NONE;
				}
			}
			else if(overlayState == Overlay::INFO)
			{
				for(int i = 3; i >= 0; i--) infoWin.update(controller[i]);

				if(votesRequested && voteTask.done)
				{
					if(voteThread.isRunning())
						voteThread.join();
					if(voteTask.success)
					{
						char likeStr[32], dislikeStr[32];
						formatVoteNumber(voteTask.vote.likes, likeStr, sizeof(likeStr));
						formatVoteNumber(voteTask.vote.dislikes, dislikeStr, sizeof(dislikeStr));
						char text[128];
						snprintf(text, sizeof(text), "Likes: %s   •   Dislikes: %s", likeStr, dislikeStr);
						votesTxt.setText(text);
					}
					else
					{
						votesTxt.setText(voteTask.err[0] ? voteTask.err : "Votes unavailable");
					}
					votesRequested = false;
				}

				if(infoCloseBtn.getState() == STATE::CLICKED)
				{
					infoCloseBtn.resetState();
					overlayState = Overlay::NONE;
				}
			}
			else if(overlayState == Overlay::SUGGESTIONS)
			{
				for(int i = 3; i >= 0; i--) suggWin.update(controller[i]);

				if(suggRequested && suggTask.suggestionsReady && !suggRowsBuilt)
				{
					suggRowsBuilt = true;

					if(suggTask.count > 0)
					{
						suggWin.remove(&suggStatusTxt);
						for(int i = 0; i < suggTask.count; i++)
						{
							snprintf(suggRowStr[i], sizeof(suggRowStr[i]), "%s", suggTask.results[i].title);
							if(strlen(suggTask.results[i].title) > 42)
							{
								suggRowStr[i][39] = '.';
								suggRowStr[i][40] = '.';
								suggRowStr[i][41] = '.';
								suggRowStr[i][42] = '\0';
							}
							if(suggTask.results[i].lengthText[0] && suggTask.results[i].author[0])
								snprintf(suggSubStr[i], sizeof(suggSubStr[i]), "%s • %s", suggTask.results[i].author, suggTask.results[i].lengthText);
							else if(suggTask.results[i].author[0])
								snprintf(suggSubStr[i], sizeof(suggSubStr[i]), "%s", suggTask.results[i].author);
							else
								suggSubStr[i][0] = '\0';

							suggRowBg[i] = new GuiImage(520, 52, (PixelColor){32, 36, 48, 255});
							suggRowBgOver[i] = new GuiImage(520, 52, (PixelColor){45, 50, 66, 255});
							suggRowBtn[i] = new GuiButton(520, 52);
							suggRowBtn[i]->setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
							suggRowBtn[i]->setPosition(0, 48 + i * 58);
							suggRowBtn[i]->setImage(suggRowBg[i]);
							suggRowBtn[i]->setImageOver(suggRowBgOver[i]);

							suggRowTxt[i] = new GuiText(suggRowStr[i], 16, white);
							suggRowTxt[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
							suggRowTxt[i]->setPosition(86, 7);
							suggRowTxt[i]->setMaxWidth(420);
							suggRowBtn[i]->setLabel(suggRowTxt[i], 0);

							if(suggSubStr[i][0])
							{
								suggSubTxt[i] = new GuiText(suggSubStr[i], 13, (PixelColor){170, 180, 205, 255});
								suggSubTxt[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
								suggSubTxt[i]->setPosition(86, 28);
								suggSubTxt[i]->setMaxWidth(420);
								suggRowBtn[i]->setLabel(suggSubTxt[i], 1);
							}

							suggRowBtn[i]->setSoundOver(&btnSoundOver2);
							suggRowBtn[i]->setTrigger(&trigA);
							suggRowBtn[i]->setEffectGrow();
							suggWin.append(suggRowBtn[i]);
						}
					}
					else if(suggTask.done)
					{
						suggStatusTxt.setText(suggTask.err[0] ? suggTask.err : "No suggestions found");
					}
				}

				if(suggRowsBuilt)
				{
					for(int i = 0; i < suggTask.count; i++)
					{
						if(suggTask.thumbReady[i] && !suggThumbImg[i] && suggTask.textures[i])
						{
							suggThumbData[i] = new GuiImageData(suggTask.textures[i], suggTask.widths[i], suggTask.heights[i], true);
							suggThumbImg[i] = new GuiImage(suggThumbData[i]);
							suggThumbImg[i]->setSize(70, 42);
							suggThumbImg[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
							suggThumbImg[i]->setPosition(8, 0);
							suggRowBtn[i]->setIcon(suggThumbImg[i]);
						}

						if(suggRowBtn[i] && suggRowBtn[i]->getState() == STATE::CLICKED)
						{
							suggTask.stop = true;
							if(suggThread.isRunning())
								suggThread.join();
							suggRequested = false;

							SetNextVideo(suggTask.results[i]);
							if(opening && task.dec)
								decAbort(task.dec);
							else if(dec)
								decAbort(dec);
							result = PLAY_NEXT_VIDEO;
							overlayState = Overlay::NONE;
							goto exitPlayLoop;
						}
					}
				}

				if(suggCloseBtn.getState() == STATE::CLICKED)
				{
					suggCloseBtn.resetState();
					overlayState = Overlay::NONE;
				}
			}

			if(seekBtn.getState() == STATE::CLICKED && scrubChan < 0 && overlayState == Overlay::NONE)
			{
				seekBtn.resetState();
				int ch = seekBtn.getStateChan();
				if(ch >= 0 && ch < 4 && info.duration > 0)
					scrubChan = ch;
			}
		}

		if(scrubChan >= 0)
		{
			double ratio = (controller[scrubChan]->getPadData().cursor_x - seekBtn.getLeft()) / (double)trackWidth;
			ratio = ratio < 0 ? 0 : (ratio > 1 ? 1 : ratio);
			scrubTarget = ratio * info.duration;

			if(!trigA.isHeld(controller[scrubChan]))
			{
				requestSeek(scrubTarget);
				scrubChan = -1;
			}
		}

		if(!started)
		{
			if(dec)
				tryStart();

			if(started && loadingUiActive)
			{
				loadingUiActive = false;
				ui.remove(&loadingTxt);
				if(info.hasVideo)
					ui.remove(&background);
			}
			else if(loadingUiActive)
			{
				int dot = (loadAnim / 15) % 4;
				if(dot == 0) loadingTxt.setText("Loading");
				else if(dot == 1) loadingTxt.setText("Loading.");
				else if(dot == 2) loadingTxt.setText("Loading..");
				else loadingTxt.setText("Loading...");
				loadAnim++;
			}
		}
		if(started && !paused && info.hasVideo)
			consumeVideo(clock());

		if(audioRunning)
		{
			while(pendingFill > 0)
			{
				fillChunk(nextChunk);
				pendingFill--;
			}
		}

		if(selectedCaptionTrack >= 0 && selectedCaptionTrack < captionTracks.count && !trackCached[selectedCaptionTrack] && capTask.done)
		{
			if(capThread.isRunning())
				capThread.join();
			if(capTask.success)
			{
				cachedCaptions[selectedCaptionTrack] = capTask.lines;
				trackCached[selectedCaptionTrack] = true;
				activeCaptions = capTask.lines;
			}
		}

		if(finished())
		{
			if(overlayState != Overlay::NONE)
			{
				if(!paused)
					togglePause();
			}
			else
			{
				if(failed)
				{
					snprintf(err, errSize, "%s", errorText);
					result = PLAY_ERROR;
				}
				break;
			}
		}

		if(failed)
		{
			if(overlayState != Overlay::NONE)
			{
				if(!paused)
					togglePause();
			}
			else
			{
				snprintf(err, errSize, "%s", errorText[0] ? errorText : "Playback error");
				result = PLAY_ERROR;
				break;
			}
		}

		double t = scrubChan >= 0 ? scrubTarget : (started ? clock() : displayTime);

		const char * currentCapText = nullptr;
		if(captionsActive && !activeCaptions.empty() && started)
		{
			uint32_t curMs = (uint32_t)(t * 1000.0);
			for(size_t i = 0; i < activeCaptions.size(); i++)
			{
				if(curMs >= activeCaptions[i].startMs && curMs < activeCaptions[i].endMs)
				{
					currentCapText = activeCaptions[i].text;
					break;
				}
			}
		}

		if(currentCapText)
		{
			if(strcmp(lastCapLine, currentCapText) != 0)
			{
				snprintf(lastCapLine, sizeof(lastCapLine), "%s", currentCapText);
				capDisp.lineCount = 0;
				capDisp.maxW = 0;
				fontSystem->setPixelSize(18);

				const char * p = lastCapLine;
				while(*p && capDisp.lineCount < 4)
				{
					const char * nextNl = strchr(p, '\n');
					int rawLen = nextNl ? (int)(nextNl - p) : (int)strlen(p);
					char rawLine[128];
					if(rawLen >= (int)sizeof(rawLine)) rawLen = sizeof(rawLine) - 1;
					memcpy(rawLine, p, rawLen);
					rawLine[rawLen] = '\0';

					int start = 0;
					while(rawLine[start] == ' ') start++;
					int end = rawLen;
					while(end > start && rawLine[end - 1] == ' ') end--;
					rawLine[end] = '\0';
					char * linePtr = rawLine + start;

					if(linePtr[0] != '\0')
					{
						if(fontSystem->getWidth(linePtr) <= 520)
						{
							snprintf(capDisp.lines[capDisp.lineCount], sizeof(capDisp.lines[capDisp.lineCount]), "%s", linePtr);
							capDisp.lineWidths[capDisp.lineCount] = fontSystem->getWidth(capDisp.lines[capDisp.lineCount]);
							if(capDisp.lineWidths[capDisp.lineCount] > capDisp.maxW)
								capDisp.maxW = capDisp.lineWidths[capDisp.lineCount];
							capDisp.lineCount++;
						}
						else
						{
							char wordsBuf[128];
							snprintf(wordsBuf, sizeof(wordsBuf), "%s", linePtr);
							char * tok = strtok(wordsBuf, " ");
							char curLine[128] = "";
							while(tok && capDisp.lineCount < 4)
							{
								char testLine[128];
								if(curLine[0] == '\0')
									snprintf(testLine, sizeof(testLine), "%s", tok);
								else
									snprintf(testLine, sizeof(testLine), "%s %s", curLine, tok);

								if(fontSystem->getWidth(testLine) <= 520)
								{
									snprintf(curLine, sizeof(curLine), "%s", testLine);
								}
								else
								{
									if(curLine[0] != '\0')
									{
										snprintf(capDisp.lines[capDisp.lineCount], sizeof(capDisp.lines[capDisp.lineCount]), "%s", curLine);
										capDisp.lineWidths[capDisp.lineCount] = fontSystem->getWidth(capDisp.lines[capDisp.lineCount]);
										if(capDisp.lineWidths[capDisp.lineCount] > capDisp.maxW)
											capDisp.maxW = capDisp.lineWidths[capDisp.lineCount];
										capDisp.lineCount++;
									}
									snprintf(curLine, sizeof(curLine), "%s", tok);
								}
								tok = strtok(nullptr, " ");
							}
							if(curLine[0] != '\0' && capDisp.lineCount < 4)
							{
								snprintf(capDisp.lines[capDisp.lineCount], sizeof(capDisp.lines[capDisp.lineCount]), "%s", curLine);
								capDisp.lineWidths[capDisp.lineCount] = fontSystem->getWidth(capDisp.lines[capDisp.lineCount]);
								if(capDisp.lineWidths[capDisp.lineCount] > capDisp.maxW)
									capDisp.maxW = capDisp.lineWidths[capDisp.lineCount];
								capDisp.lineCount++;
							}
						}
					}

					if(!nextNl) break;
					p = nextNl + 1;
				}

				capDisp.totalH = capDisp.lineCount > 0 ? (capDisp.lineCount * 22 + 8) : 0;
			}
		}
		else if(lastCapLine[0] != '\0')
		{
			lastCapLine[0] = '\0';
			capDisp.lineCount = 0;
		}

		int fillW = info.duration > 0 ? 1 + (int)((trackWidth - 1) * (t > info.duration ? 1 : (t < 0 ? 0 : t / info.duration))) : 1;
		trackFill.setSize(fillW, 8);
		knob.setPosition(TRACK_MARGIN - 6 + fillW, -115);

		int second = t > 0 ? (int)t : 0;
		if(second != lastSecond)
		{
			char text[32];
			int total = (int)info.duration;
			snprintf(text, sizeof(text), "%d:%02d / %d:%02d", second / 60, second % 60, total / 60, total % 60);
			timeTxt.setText(text);
			lastSecond = second;
		}

		if(volume != lastVolume)
		{
			char text[16];
			snprintf(text, sizeof(text), "Vol %d%%", volume * 100 / 255);
			volTxt.setText(text);
			lastVolume = volume;
		}

		if(paused != lastPaused)
		{
			playBtn.icon.setImage(paused ? &iconPlay : &iconPause);
			lastPaused = paused;
		}

		if(info.hasVideo)
		{
			lock.lock();
			int slot = shown;
			lock.unlock();

			if(slot >= 0)
			{
				yuvDraw(&frames[slot]);
				ogcVideo->resetVideoMenu();
			}
		}

		if(captionsActive && capDisp.lineCount > 0)
		{
			int boxW = capDisp.maxW + 20;
			if(boxW < 60) boxW = 60;
			int boxH = capDisp.totalH;
			int boxX = (screenWidth - boxW) / 2;
			int bottomY = uiVisible ? (screenHeight - 145) : (screenHeight - 45);
			int boxY = bottomY - boxH;

			platform->getVideo()->getImageRenderer()->drawRectangle(boxX, boxY, boxW, boxH, (PixelColor){0, 0, 0, 180});

			fontSystem->setPixelSize(18);
			for(int i = 0; i < capDisp.lineCount; i++)
			{
				int lineY = boxY + 4 + i * 22;
				fontSystem->drawText(screenWidth / 2, lineY, capDisp.lines[i], white, GUI_TEXT_JUSTIFY_CENTER | GUI_TEXT_ALIGN_TOP);
			}
		}

		if(uiVisible)
		{
			ui.draw();

			if(overlayState != Overlay::NONE)
			{
				overlayDim.draw();
				if(overlayState == Overlay::MENU) menuWin.draw();
				else if(overlayState == Overlay::INFO) infoWin.draw();
				else if(overlayState == Overlay::SUGGESTIONS) suggWin.draw();
			}

			DrawPointers();
		}

		video->render();
	}

exitPlayLoop:
	active = nullptr;
	if(audioRunning)
	{
		ASND_StopVoice(AUDIO_VOICE);
		audioRunning = false;
		pendingFill = 0;
	}

	if(opening)
	{
		if(task.dec)
			decAbort(task.dec);
		openThread.join();
		if(task.dec)
		{
			decClose(task.dec);
			task.dec = nullptr;
		}
	}

	pfpStop = true;
	if(pfpThread.isRunning())
		pfpThread.join();

	if(voteThread.isRunning())
		voteThread.join();

	suggTask.stop = true;
	if(suggThread.isRunning())
		suggThread.join();

	if(capThread.isRunning())
		capThread.join();

	suggWin.removeAll();
	menuWin.removeAll();
	infoWin.removeAll();

	for(int i = 0; i < MAX_SUGGESTIONS; i++)
	{
		delete suggRowBtn[i];
		delete suggRowTxt[i];
		delete suggSubTxt[i];
		delete suggRowBg[i];
		delete suggRowBgOver[i];
		delete suggThumbImg[i];
		if(suggThumbData[i])
		{
			delete suggThumbData[i];
			suggTask.textures[i] = nullptr;
		}
		if(suggTask.textures[i])
		{
			platform->getVideo()->getImageRenderer()->destroyTexture(suggTask.textures[i]);
			suggTask.textures[i] = nullptr;
		}
	}

	if(pfpTexture)
	{
		platform->getVideo()->getImageRenderer()->destroyTexture(pfpTexture);
		pfpTexture = nullptr;
	}

	if(pfpImg)
		ui.remove(pfpImg);
	delete pfpImg;
	pfpImg = nullptr;
	delete pfpData;
	pfpData = nullptr;

	ui.removeAll();

	return result;
}

}

PlayResult PlayFile(const char * path, char * err, int errSize, const char * customTitle, const YtResult * ytMeta)
{
	Player player(path, customTitle, ytMeta);
	return player.run(err, errSize);
}

static char sNextChannelId[64] = "";
static char sNextChannelAuthor[128] = "";
static char sNextChannelAvatar[256] = "";

void SetNextChannel(const char * channelId, const char * author, const char * avatarUrl)
{
	snprintf(sNextChannelId, sizeof(sNextChannelId), "%s", channelId ? channelId : "");
	snprintf(sNextChannelAuthor, sizeof(sNextChannelAuthor), "%s", author ? author : "");
	snprintf(sNextChannelAvatar, sizeof(sNextChannelAvatar), "%s", avatarUrl ? avatarUrl : "");
}

bool GetNextChannel(char * channelIdOut, int channelIdSize, char * authorOut, int authorSize, char * avatarUrlOut, int avatarUrlSize)
{
	if(sNextChannelId[0] == '\0' && sNextChannelAuthor[0] == '\0')
		return false;
	if(channelIdOut && channelIdSize > 0)
		snprintf(channelIdOut, channelIdSize, "%s", sNextChannelId);
	if(authorOut && authorSize > 0)
		snprintf(authorOut, authorSize, "%s", sNextChannelAuthor);
	if(avatarUrlOut && avatarUrlSize > 0)
		snprintf(avatarUrlOut, avatarUrlSize, "%s", sNextChannelAvatar);
	sNextChannelId[0] = '\0';
	sNextChannelAuthor[0] = '\0';
	sNextChannelAvatar[0] = '\0';
	return true;
}

static YtResult sNextVideo;
static bool sHasNextVideo = false;

void SetNextVideo(const YtResult & video)
{
	sNextVideo = video;
	sHasNextVideo = true;
}

bool GetNextVideo(YtResult * videoOut)
{
	if(!sHasNextVideo)
		return false;
	if(videoOut)
		*videoOut = sNextVideo;
	sHasNextVideo = false;
	return true;
}
