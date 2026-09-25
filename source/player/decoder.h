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

#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEC_TEXT_LEN 96

typedef struct Decoder Decoder;

typedef struct {
	int hasVideo;
	int hasAudio;
	int width, height;
	int sarNum, sarDen;
	int sampleRate;
	double duration;
	char title[DEC_TEXT_LEN];
	char artist[DEC_TEXT_LEN];
	char album[DEC_TEXT_LEN];
} MediaInfo;

typedef enum {
	DEC_VIDEO,
	DEC_AUDIO,
	DEC_EOF,
	DEC_ERROR
} DecResult;

typedef struct {
	double pts;
	uint8_t *planes[3];
	int strides[3];
	int16_t *pcm;
	int frames;
	int sampleRate;
} DecFrame;

Decoder *decOpen(const char *path, MediaInfo *info, char *err, int errSize);
void decClose(Decoder *d);
DecResult decNext(Decoder *d, DecFrame *out);
int decSeek(Decoder *d, double seconds);
void decSetFast(Decoder *d, int fast);
const char *decError(Decoder *d);

#ifdef __cplusplus
}
#endif

#endif
