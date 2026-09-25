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

#ifndef YOUTUBE_H
#define YOUTUBE_H

#define YT_MAX_RESULTS 10

struct YtResult
{
	char videoId[16];
	char title[160];
	char author[96];
	char lengthText[16];
	char viewCountText[32];
	char publishedText[32];
};

int ytSearch(const char *query, YtResult *results, int maxResults, char *err, int errSize);
bool ytResolveStream(const char *videoId, char *urlOut, int urlOutSize, char *err, int errSize);
void *ytFetchThumbnail(const char *videoId, int maxW, int maxH, int *outW, int *outH);

#endif
