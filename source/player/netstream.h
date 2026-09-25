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

#ifndef NETSTREAM_H
#define NETSTREAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NetStream NetStream;

int netInit(void);
int netIsUrl(const char *path);
NetStream *netOpen(const char *url, char *err, int errSize);
void netClose(NetStream *n);
int netRead(NetStream *n, uint8_t *buf, int size);
int64_t netSeek(NetStream *n, int64_t offset, int whence);
int64_t netSize(NetStream *n);

#ifdef __cplusplus
}
#endif

#endif
