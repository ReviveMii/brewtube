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
		volatile uint32_t callbacks = 0;
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
	if(audioRunning)
		ASND_StopVoice(AUDIO_VOICE);

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

	active = nullptr;
	if(dec)
		decClose(dec);
	free(ring);
	for(int i = 0; i < AUDIO_BUFFERS; i++)
		free(chunk[i]);
	if(slotCount > 0)
		yuvClose(frames, slotCount);
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
	if(!thread.start(decodeEntry, this, 128 * 1024, ThreadPriority::Low))
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
	if(!ytMeta || !ytMeta->channelId[0])
		return;

	int w = 0, h = 0;
	void * tex = ytFetchChannelPfp(ytMeta->channelId, 48, 48, &w, &h);
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
	lock.unlock();

	uint32_t pos = ringWr & (RING_FRAMES - 1);
	uint32_t first = RING_FRAMES - pos < n ? RING_FRAMES - pos : n;
	memcpy(ring + pos * 2, f.pcm, first * 4);
	memcpy(ring, f.pcm + first * 2, (n - first) * 4);
	BARRIER();
	ringWr = ringWr + n;
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

	fillChunk(nextChunk);
	if(ASND_AddVoice(voice, chunk[nextChunk], AUDIO_CHUNK * 4) == SND_OK)
		nextChunk = (nextChunk + 1) % AUDIO_BUFFERS;
}

void Player::startAudio()
{
	fillChunk(0);
	fillChunk(1);
	playingChunk = 0;
	nextChunk = 2;
	lastCallback = gettime();
	ASND_SetVoice(AUDIO_VOICE, VOICE_STEREO_16BIT, info.sampleRate, 0, chunk[0], AUDIO_CHUNK * 4, volume, volume, audioCallback);
	ASND_AddVoice(AUDIO_VOICE, chunk[1], AUDIO_CHUNK * 4);
	audioRunning = true;
}

void Player::tryStart()
{
	lock.lock();
	uint32_t avail = ringWr - ringRd;
	int need = slotCount < 4 ? slotCount - 1 : 2;
	bool ready = !seekPending;
	double firstPts = fifoCount > 0 ? framePts[fifo[fifoHead]] : 0;

	if(ready && info.hasAudio)
		ready = avail >= AUDIO_CHUNK * 2 || eof;
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

	if(ytMeta && ytMeta->channelId[0])
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

	UiButton stopBtn(&btnSmall, &btnSmallOver, &iconStop, -250, &trigA);
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
			if(ytMeta->author[0]) ui.append(&chanTxt);
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
	ui.append(&backBtn.button);
	ui.append(&playBtn.button);
	ui.append(&fwdBtn.button);
	ui.append(&volDownBtn.button);
	ui.append(&volUpBtn.button);
	ui.append(&loadingTxt);

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
		bool uiVisible = !started || !info.hasVideo || paused || idle < UI_HIDE_FRAMES;

		if(pressed & (INPUT_BTN_B | INPUT_BTN_1))
		{
			if(opening && task.dec)
				decAbort(task.dec);
			else if(dec)
				decAbort(dec);
			break;
		}
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

			if(seekBtn.getState() == STATE::CLICKED && scrubChan < 0)
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

		if(finished())
		{
			if(failed)
			{
				snprintf(err, errSize, "%s", errorText);
				result = PLAY_ERROR;
			}
			break;
		}

		if(failed)
		{
			snprintf(err, errSize, "%s", errorText[0] ? errorText : "Playback error");
			result = PLAY_ERROR;
			break;
		}

		double t = scrubChan >= 0 ? scrubTarget : (started ? clock() : displayTime);
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

		if(uiVisible)
		{
			ui.draw();
			DrawPointers();
		}

		video->render();
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
