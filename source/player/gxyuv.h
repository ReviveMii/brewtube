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

#ifndef GXYUV_H
#define GXYUV_H

#include <gccore.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	u8 *y;
	u8 *u;
	u8 *v;
} YuvFrame;

int yuvOpen(GXRModeObj *mode, int width, int height, int sarNum, int sarDen, int wide, YuvFrame *frames, int count);
void yuvClose(YuvFrame *frames, int count);
void yuvFill(const YuvFrame *frame, u8 *const planes[3], const int strides[3]);
void yuvDraw(const YuvFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
