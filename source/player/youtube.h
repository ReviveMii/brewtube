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

#include <stdint.h>
#include <vector>

#define YT_MAX_RESULTS 30
#define YT_MAX_CAPTION_TRACKS 16

struct YtCaptionTrack
{
	char name[48];
	char languageCode[16];
	char baseUrl[1024];
};

struct YtCaptionTrackList
{
	int count = 0;
	YtCaptionTrack tracks[YT_MAX_CAPTION_TRACKS];
};

struct YtCaptionLine
{
	uint32_t startMs;
	uint32_t endMs;
	char text[256];
};

struct YtResult
{
	char videoId[16];
	char channelId[64];
	char title[160];
	char author[96];
	char lengthText[16];
	char viewCountText[32];
	char publishedText[32];
	char avatarUrl[256];
	char description[512];
	bool isChannel;
};

struct YtVoteData
{
	long likes;
	long dislikes;
};

enum YtClientType
{
	YT_CLIENT_ANDROID = 0,
	YT_CLIENT_VISIONOS = 1
};

void ytSetClient(YtClientType client);
YtClientType ytGetClient();

enum YtChannelTab
{
	YT_CHAN_TAB_VIDEOS = 0,
	YT_CHAN_TAB_SHORTS,
	YT_CHAN_TAB_PLAYLISTS,
	YT_CHAN_TAB_POSTS,
	YT_CHAN_TAB_ABOUT
};

enum YtChannelFilter
{
	YT_CHAN_FILTER_NEWEST = 0,
	YT_CHAN_FILTER_POPULAR,
	YT_CHAN_FILTER_OLDEST
};

struct YtChannelItem
{
	char id[64];
	char title[160];
	char duration[16];
	char views[32];
	char date[32];
	char thumbUrl[256];
	bool isPlayable;
};

struct YtChannelDetails
{
	char channelId[64];
	char title[96];
	char handle[64];
	char subscriberCount[32];
	char videoCount[32];
	char description[1024];
	char avatarUrl[256];
	char bannerUrl[256];
	char latestToken[256];
	char popularToken[256];
	char oldestToken[256];
};

int ytSearch(const char *query, YtResult *results, int maxResults, char *err, int errSize,
	const char *continuation = nullptr, char *nextContinuationOut = nullptr, int nextContinuationOutSize = 0);
int ytGetSuggestions(const char *videoId, YtResult *results, int maxResults, char *err, int errSize);
bool ytFetchVoteData(const char *videoId, YtVoteData *voteOut, char *err, int errSize);
bool ytResolveStream(const char *videoId, char *urlOut, int urlOutSize, char *err, int errSize, YtResult *infoOut = nullptr);
void *ytFetchThumbnail(const char *videoId, int maxW, int maxH, int *outW, int *outH);
void *ytFetchChannelPfp(const char *channelIdOrUrl, int maxW, int maxH, int *outW, int *outH);
void *ytFetchImage(const char *url, int maxW, int maxH, int *outW, int *outH);
bool ytChannelBrowse(const char *channelIdOrHandle, YtChannelDetails *details, YtChannelTab tab, YtChannelFilter filter, YtChannelItem *items, int maxItems, int *outCount, char *err, int errSize);
void ytStartUpdateCheck();
bool ytIsUpdateAvailable();
bool ytGetCaptionTracks(YtCaptionTrackList *out);
bool ytFetchCaptions(const char *url, std::vector<YtCaptionLine> &linesOut);

#endif
