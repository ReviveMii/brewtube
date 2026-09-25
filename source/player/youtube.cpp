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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <wiisocket.h>
#include <gctypes.h>

#include "drivers/Platform.h"
#include "drivers/Thread.h"
#include "youtube.h"
#include "netstream.h"
#include "json.h"
#include "cacert_pem.h"
#include "JPEGDEC.h"

namespace {

struct curl_blob caInfo = { (u8 *)cacert_pem, cacert_pem_size, CURL_BLOB_COPY };

bool netReady()
{
	return netInit() != 0;
}

size_t writeCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	std::string *out = static_cast<std::string *>(userdata);
	out->append(ptr, size * nmemb);
	return size * nmemb;
}

CURL *newCurl(const char *url, std::string *response)
{
	if(!netReady())
		return nullptr;

	CURL *curl = curl_easy_init();
	if(!curl)
		return nullptr;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &caInfo);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "HTTP,HTTPS");
	return curl;
}

bool httpPost(const char *url, const char *body, std::string &response)
{
	CURL *curl = newCurl(url, &response);
	if(!curl)
		return false;

	curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	CURLcode res = curl_easy_perform(curl);
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	return res == CURLE_OK && httpCode == 200;
}

bool httpGetBinary(const char *url, std::string &response, long timeoutSec = 4)
{
	CURL *curl = newCurl(url, &response);
	if(!curl)
		return false;

	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);

	CURLcode res = curl_easy_perform(curl);
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(curl);

	return res == CURLE_OK && httpCode == 200;
}

std::string getVisitorData()
{
	static std::string cached;
	if(!cached.empty())
		return cached;

	std::string response;
	CURL *curl = newCurl("https://www.youtube.com/sw.js_data", &response);
	if(!curl)
		return "";

	curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Origin: https://www.youtube.com");
	headers = curl_slist_append(headers, "Referer: https://www.youtube.com/");
	headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);

	CURLcode res = curl_easy_perform(curl);
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if(res != CURLE_OK || httpCode != 200)
		return "";

	const char *p = response.c_str();
	while(*p && (*p == ')' || *p == ']' || *p == '}' || *p == '\'' || *p == '\r' || *p == '\n' || *p == ' ' || *p == '\t'))
		p++;

	JsonValue root;
	if(jsonParse(p, strlen(p), root))
	{
		const JsonValue *v0 = root.at(0);
		const JsonValue *v1 = v0 ? v0->at(2) : nullptr;
		const JsonValue *v2 = v1 ? v1->at(0) : nullptr;
		const JsonValue *v3 = v2 ? v2->at(0) : nullptr;
		const JsonValue *v4 = v3 ? v3->at(13) : nullptr;
		if(v4)
		{
			cached = v4->asString("");
			return cached;
		}
	}
	return "";
}

std::string jsonEscape(const char *s)
{
	std::string out;
	for(const char *c = s; *c; c++)
	{
		if(*c == '"' || *c == '\\')
		{
			out += '\\';
			out += *c;
		}
		else if(*c == '\n')
			out += "\\n";
		else if(*c == '\r')
			out += "\\r";
		else if(*c == '\t')
			out += "\\t";
		else
			out += *c;
	}
	return out;
}

void collectVideoRenderers(const JsonValue &node, std::vector<const JsonValue *> &out, int maxResults)
{
	if((int)out.size() >= maxResults)
		return;

	if(node.type == JsonType::Object)
	{
		const JsonValue *vr = node.get("videoRenderer");
		if(vr && vr->get("videoId"))
		{
			out.push_back(vr);
			return;
		}
		for(const auto &kv : node.obj)
			collectVideoRenderers(kv.second, out, maxResults);
	}
	else if(node.type == JsonType::Array)
	{
		for(const auto &v : node.arr)
			collectVideoRenderers(v, out, maxResults);
	}
}

const char *firstRunText(const JsonValue *node, const char *def = "")
{
	if(!node)
		return def;
	const JsonValue *runs = node->get("runs");
	if(runs && runs->size() > 0)
	{
		const JsonValue *first = runs->at(0);
		if(first && first->get("text"))
			return first->get("text")->asString(def);
	}
	const JsonValue *simple = node->get("simpleText");
	if(simple)
		return simple->asString(def);
	return def;
}

const char *extractChannelId(const JsonValue *vr)
{
	if(!vr) return "";
	const char *fields[] = { "ownerText", "shortBylineText", "longBylineText" };
	for(const char *f : fields)
	{
		const JsonValue *runs = vr->get(f) ? vr->get(f)->get("runs") : nullptr;
		if(runs && runs->size() > 0)
		{
			const JsonValue *ep = runs->at(0)->get("navigationEndpoint");
			const JsonValue *bp = ep ? ep->get("browseEndpoint") : nullptr;
			if(bp && bp->get("browseId"))
				return bp->get("browseId")->asString("");
		}
	}
	const JsonValue *cts = vr->get("channelThumbnailSupportedRenderers");
	const JsonValue *ctlr = cts ? cts->get("channelThumbnailWithLinkRenderer") : nullptr;
	const JsonValue *ep = ctlr ? ctlr->get("navigationEndpoint") : nullptr;
	const JsonValue *bp = ep ? ep->get("browseEndpoint") : nullptr;
	if(bp && bp->get("browseId"))
		return bp->get("browseId")->asString("");
	return "";
}

struct JpegDecodeContext
{
	int width;
	int height;
	std::vector<uint8_t> rgba;
};

int jpegDrawCallback(JPEGDRAW *pDraw)
{
	JpegDecodeContext *ctx = static_cast<JpegDecodeContext *>(pDraw->pUser);
	int blockW = pDraw->iWidth;
	int blockH = pDraw->iHeight;
	const uint16_t *src = pDraw->pPixels;

	for(int y = 0; y < blockH; y++)
	{
		int dstY = pDraw->y + y;
		if(dstY >= ctx->height)
			break;

		for(int x = 0; x < blockW; x++)
		{
			int dstX = pDraw->x + x;
			if(dstX >= ctx->width)
				break;

			uint16_t pix = src[y * blockW + x];
			size_t idx = (size_t)(dstY * ctx->width + dstX) * 4;
			ctx->rgba[idx + 0] = ((pix >> 11) & 0x1f) * 255 / 31;
			ctx->rgba[idx + 1] = ((pix >> 5) & 0x3f) * 255 / 63;
			ctx->rgba[idx + 2] = (pix & 0x1f) * 255 / 31;
			ctx->rgba[idx + 3] = 255;
		}
	}
	return 1;
}

void *decodeJpegToTexture(const uint8_t *data, size_t size, int maxW, int maxH, int *outW, int *outH)
{
	if(!data || size == 0)
		return nullptr;

	JPEGDEC jpeg;
	if(!jpeg.openRAM((uint8_t *)data, (int)size, jpegDrawCallback))
		return nullptr;

	int srcW = jpeg.getWidth();
	int srcH = jpeg.getHeight();
	if(srcW <= 0 || srcH <= 0)
	{
		jpeg.close();
		return nullptr;
	}

	JpegDecodeContext ctx;
	ctx.width = srcW;
	ctx.height = srcH;
	ctx.rgba.resize((size_t)srcW * srcH * 4);

	jpeg.setUserPointer(&ctx);
	if(!jpeg.decode(0, 0, 0))
	{
		jpeg.close();
		return nullptr;
	}
	jpeg.close();

	double scale = 1.0;
	if(maxW > 0 && srcW > maxW)
		scale = (double)maxW / srcW;
	if(maxH > 0 && srcH * scale > maxH)
		scale = (double)maxH / srcH;

	int dstW = scale < 1.0 ? (int)(srcW * scale) : srcW;
	int dstH = scale < 1.0 ? (int)(srcH * scale) : srcH;
	if(dstW < 1) dstW = 1;
	if(dstH < 1) dstH = 1;

	std::vector<uint8_t> scaled;
	uint8_t *finalPixels = nullptr;

	if(dstW == srcW && dstH == srcH)
	{
		finalPixels = ctx.rgba.data();
	}
	else
	{
		scaled.resize((size_t)dstW * dstH * 4);
		for(int y = 0; y < dstH; y++)
		{
			int sy = y * srcH / dstH;
			for(int x = 0; x < dstW; x++)
			{
				int sx = x * srcW / dstW;
				size_t sIdx = (size_t)(sy * srcW + sx) * 4;
				size_t dIdx = (size_t)(y * dstW + x) * 4;
				scaled[dIdx + 0] = ctx.rgba[sIdx + 0];
				scaled[dIdx + 1] = ctx.rgba[sIdx + 1];
				scaled[dIdx + 2] = ctx.rgba[sIdx + 2];
				scaled[dIdx + 3] = 255;
			}
		}
		finalPixels = scaled.data();
	}

	void *texture = platform->getVideo()->getImageRenderer()->createTexture(dstW, dstH);
	if(texture)
	{
		platform->getVideo()->getImageRenderer()->loadTextureData(texture, finalPixels, dstW, dstH);
		if(outW) *outW = dstW;
		if(outH) *outH = dstH;
	}

	return texture;
}

}

int ytSearch(const char *query, YtResult *results, int maxResults, char *err, int errSize)
{
	std::string body = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260925.01.00\",\"hl\":\"en\",\"gl\":\"US\"}},\"query\":\"";
	body += jsonEscape(query);
	body += "\"}";

	std::string response;
	const char *url = "https://www.youtube.com/youtubei/v1/search";

	if(!httpPost(url, body.c_str(), response))
	{
		snprintf(err, errSize, "Could not reach YouTube");
		return 0;
	}

	JsonValue root;
	if(!jsonParse(response.c_str(), response.size(), root))
	{
		snprintf(err, errSize, "Unexpected response from YouTube");
		return 0;
	}

	std::vector<const JsonValue *> renderers;
	collectVideoRenderers(root, renderers, maxResults);

	int count = 0;
	for(const JsonValue *vr : renderers)
	{
		if(count >= maxResults)
			break;

		YtResult &r = results[count];
		const char *vid = vr->get("videoId") ? vr->get("videoId")->asString("") : "";
		snprintf(r.videoId, sizeof(r.videoId), "%s", vid);
		if(r.videoId[0] == '\0')
			continue;

		snprintf(r.channelId, sizeof(r.channelId), "%s", extractChannelId(vr));
		snprintf(r.title, sizeof(r.title), "%s", firstRunText(vr->get("title"), "(untitled)"));

		const char *author = firstRunText(vr->get("ownerText"), "");
		if(!author || author[0] == '\0')
			author = firstRunText(vr->get("longBylineText"), "");
		if(!author || author[0] == '\0')
			author = firstRunText(vr->get("shortBylineText"), "");
		snprintf(r.author, sizeof(r.author), "%s", author ? author : "");

		snprintf(r.lengthText, sizeof(r.lengthText), "%s", firstRunText(vr->get("lengthText"), "Live"));
		snprintf(r.viewCountText, sizeof(r.viewCountText), "%s", firstRunText(vr->get("viewCountText"), ""));
		snprintf(r.publishedText, sizeof(r.publishedText), "%s", firstRunText(vr->get("publishedTimeText"), ""));
		count++;
	}

	if(count == 0)
		snprintf(err, errSize, "No results found");

	return count;
}

static YtClientType gClientType = YT_CLIENT_ANDROID;

void ytSetClient(YtClientType client)
{
	gClientType = client;
}

YtClientType ytGetClient()
{
	return gClientType;
}

bool ytResolveStream(const char *videoId, char *urlOut, int urlOutSize, char *err, int errSize, YtResult *infoOut)
{
	std::string visitorData = getVisitorData();
	const char *playerUrl = "https://www.youtube.com/youtubei/v1/player";

	auto extractMeta = [&](const JsonValue & root) {
		if(!infoOut) return;
		const JsonValue *vd = root.get("videoDetails");
		if(vd)
		{
			if(infoOut->channelId[0] == '\0' && vd->get("channelId"))
				snprintf(infoOut->channelId, sizeof(infoOut->channelId), "%s", vd->get("channelId")->asString(""));
			if(infoOut->author[0] == '\0' && vd->get("author"))
				snprintf(infoOut->author, sizeof(infoOut->author), "%s", vd->get("author")->asString(""));
			if(infoOut->title[0] == '\0' && vd->get("title"))
				snprintf(infoOut->title, sizeof(infoOut->title), "%s", vd->get("title")->asString(""));
			if(infoOut->viewCountText[0] == '\0' && vd->get("viewCount"))
				snprintf(infoOut->viewCountText, sizeof(infoOut->viewCountText), "%s views", vd->get("viewCount")->asString(""));
		}
		const JsonValue *mf = root.get("microformat");
		const JsonValue *pmr = mf ? mf->get("playerMicroformatRenderer") : nullptr;
		if(pmr && infoOut->publishedText[0] == '\0')
		{
			const JsonValue *ud = pmr->get("uploadDate");
			if(!ud) ud = pmr->get("publishDate");
			if(ud) snprintf(infoOut->publishedText, sizeof(infoOut->publishedText), "%s", ud->asString(""));
		}
	};

	auto tryAndroid = [&]() -> bool {
		std::string aBody;
		aBody.reserve(2048);
		aBody += "{\"videoId\":\"";
		aBody += videoId;
		aBody += "\",\"context\":{\"client\":{\"clientName\":\"ANDROID\",\"clientVersion\":\"21.26.364\",\"androidSdkVersion\":30,\"userAgent\":\"com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip\",\"osName\":\"Android\",\"osVersion\":\"11\",\"hl\":\"en\",\"gl\":\"US\"";
		if(!visitorData.empty())
		{
			aBody += ",\"visitorData\":\"";
			aBody += jsonEscape(visitorData.c_str());
			aBody += "\"";
		}
		aBody += "}},\"contentCheckOk\":true,\"racyCheckOk\":true}";

		std::string response;
		if(!httpPost(playerUrl, aBody.c_str(), response))
		{
			snprintf(err, errSize, "Could not reach YouTube");
			return false;
		}

		JsonValue root;
		if(!jsonParse(response.c_str(), response.size(), root))
		{
			snprintf(err, errSize, "Unexpected response from YouTube");
			return false;
		}

		extractMeta(root);

		const JsonValue *status = root.get("playabilityStatus");
		const char *statusText = status && status->get("status") ? status->get("status")->asString("") : "";
		if(strcmp(statusText, "OK") != 0)
		{
			const JsonValue *reason = status ? status->get("reason") : nullptr;
			snprintf(err, errSize, "%s", reason ? reason->asString("Video unavailable") : "Video unavailable");
			return false;
		}

		const JsonValue *streamingData = root.get("streamingData");
		const JsonValue *formats = streamingData ? streamingData->get("formats") : nullptr;
		if(!formats)
		{
			snprintf(err, errSize, "No playable formats returned");
			return false;
		}

		for(size_t i = 0; i < formats->size(); i++)
		{
			const JsonValue *fmt = formats->at(i);
			if(!fmt || fmt->get("itag")->asInt(-1) != 18)
				continue;

			const JsonValue *urlNode = fmt->get("url");
			if(!urlNode || urlNode->asString("")[0] == '\0')
			{
				snprintf(err, errSize, "Stream requires signature decryption, unsupported");
				return false;
			}

			snprintf(urlOut, urlOutSize, "%s", urlNode->asString(""));
			return true;
		}

		snprintf(err, errSize, "itag 18 not available for this video");
		return false;
	};

	auto tryVisionOs = [&]() -> bool {
		std::string vBody;
		vBody.reserve(2048);
		vBody += "{\"videoId\":\"";
		vBody += videoId;
		vBody += "\",\"context\":{\"client\":{\"clientName\":\"VISIONOS\",\"clientVersion\":\"1.02\",\"deviceMake\":\"Apple\",\"deviceModel\":\"RealityDevice17,1\",\"userAgent\":\"Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15\",\"osName\":\"visionOS\",\"osVersion\":\"26.5.23O471\",\"hl\":\"en\",\"gl\":\"US\"";
		if(!visitorData.empty())
		{
			vBody += ",\"visitorData\":\"";
			vBody += jsonEscape(visitorData.c_str());
			vBody += "\"";
		}
		vBody += "}},\"contentCheckOk\":true,\"racyCheckOk\":true}";

		std::string vResp;
		if(!httpPost(playerUrl, vBody.c_str(), vResp))
			return false;

		JsonValue vRoot;
		if(!jsonParse(vResp.c_str(), vResp.size(), vRoot))
			return false;

		extractMeta(vRoot);

		const JsonValue *status = vRoot.get("playabilityStatus");
		const char *statusText = status && status->get("status") ? status->get("status")->asString("") : "";
		if(strcmp(statusText, "OK") != 0)
			return false;

		const JsonValue *streamingData = vRoot.get("streamingData");
		const JsonValue *adaptive = streamingData ? streamingData->get("adaptiveFormats") : nullptr;
		if(!adaptive)
			return false;

		const char *bestVideoUrl = nullptr;
		int bestVideoHeight = 0;
		int bestVideoBitrate = 0;

		const char *bestAudioUrl = nullptr;
		int bestAudioBitrate = 0;
		int bestAudioPriority = -1;

		auto containsCaseInsensitive = [](const char *haystack, const char *needle) -> bool {
			if(!haystack || !needle) return false;
			size_t nlen = strlen(needle);
			if(nlen == 0) return true;
			size_t hlen = strlen(haystack);
			if(hlen < nlen) return false;
			for(size_t i = 0; i <= hlen - nlen; i++)
			{
				if(strncasecmp(haystack + i, needle, nlen) == 0)
					return true;
			}
			return false;
		};

		for(size_t i = 0; i < adaptive->size(); i++)
		{
			const JsonValue *fmt = adaptive->at(i);
			if(!fmt) continue;

			const JsonValue *urlNode = fmt->get("url");
			if(!urlNode) continue;
			const char *u = urlNode->asString("");
			if(u[0] == '\0') continue;

			const char *mime = fmt->get("mimeType") ? fmt->get("mimeType")->asString("") : "";
			int bitrate = fmt->get("bitrate") ? fmt->get("bitrate")->asInt(0) : 0;

			if((strstr(mime, "video/mp4") != nullptr || strstr(mime, "avc1") != nullptr || strstr(mime, "h264") != nullptr)
				&& strstr(mime, "vp9") == nullptr && strstr(mime, "vp09") == nullptr && strstr(mime, "av01") == nullptr)
			{
				int h = fmt->get("height") ? fmt->get("height")->asInt(0) : 0;
				if(h == 0 && fmt->get("qualityLabel"))
					h = atoi(fmt->get("qualityLabel")->asString(""));

				if(h > 0 && h <= 480)
				{
					if(h > bestVideoHeight || (h == bestVideoHeight && bitrate > bestVideoBitrate))
					{
						bestVideoHeight = h;
						bestVideoBitrate = bitrate;
						bestVideoUrl = u;
					}
				}
			}
			else if((strstr(mime, "audio/mp4") != nullptr || strstr(mime, "mp4a") != nullptr || strstr(mime, "aac") != nullptr)
				&& strstr(mime, "opus") == nullptr && strstr(mime, "webm") == nullptr)
			{
				int trackPriority = 1;
				const JsonValue *audioTrack = fmt->get("audioTrack");
				if(audioTrack)
				{
					bool isDefault = audioTrack->get("audioIsDefault") ? audioTrack->get("audioIsDefault")->asBool(false) : false;
					const char *dispName = audioTrack->get("displayName") ? audioTrack->get("displayName")->asString("") : "";
					const char *trackId = audioTrack->get("id") ? audioTrack->get("id")->asString("") : "";
					bool isOriginal = isDefault || containsCaseInsensitive(dispName, "original") || containsCaseInsensitive(trackId, "original");
					trackPriority = isOriginal ? 2 : 0;
				}

				if(trackPriority > bestAudioPriority || (trackPriority == bestAudioPriority && bitrate > bestAudioBitrate))
				{
					bestAudioPriority = trackPriority;
					bestAudioBitrate = bitrate;
					bestAudioUrl = u;
				}
			}
		}

		if(bestVideoUrl && bestAudioUrl)
		{
			snprintf(urlOut, urlOutSize, "%s\n%s", bestVideoUrl, bestAudioUrl);
			return true;
		}

		return false;
	};

	if(gClientType == YT_CLIENT_ANDROID)
	{
		if(tryAndroid())
			return true;
		if(tryVisionOs())
			return true;
		return false;
	}
	else
	{
		if(tryVisionOs())
			return true;
		if(tryAndroid())
			return true;
		return false;
	}
}

void *ytFetchThumbnail(const char *videoId, int maxW, int maxH, int *outW, int *outH)
{
	char url[128];
	snprintf(url, sizeof(url), "https://i.ytimg.com/vi/%s/mqdefault.jpg", videoId);

	std::string jpegData;
	if(!httpGetBinary(url, jpegData) || jpegData.empty())
	{
		snprintf(url, sizeof(url), "https://i.ytimg.com/vi/%s/default.jpg", videoId);
		if(!httpGetBinary(url, jpegData) || jpegData.empty())
			return nullptr;
	}

	return decodeJpegToTexture((const uint8_t *)jpegData.data(), jpegData.size(), maxW, maxH, outW, outH);
}

void *ytFetchChannelPfp(const char *channelId, int maxW, int maxH, int *outW, int *outH)
{
	if(!channelId || channelId[0] == '\0')
		return nullptr;

	std::string url = "https://www.youtube.com/channel/";
	url += channelId;

	std::string html;
	CURL *curl = newCurl(url.c_str(), &html);
	if(!curl)
		return nullptr;

	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
	curl_easy_setopt(curl, CURLOPT_COOKIE, "SOCS=CAISNQgDEitib3FfaWRlbnRpdHlmcm9udGVuZHVpc2VydmVyXzIwMjYwNzE0LjA3X3AwGgJkZSACGgYIgL7g0gY; PREF=f6=40000000&tz=Europe.Berlin");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

	CURLcode res = curl_easy_perform(curl);
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(curl);

	if(res != CURLE_OK || httpCode < 200 || httpCode >= 400 || html.empty())
		return nullptr;

	const char *pattern = "<meta property=\"og:image\" content=\"";
	size_t pos = html.find(pattern);
	if(pos == std::string::npos)
	{
		pattern = "property=\"og:image\" content=\"";
		pos = html.find(pattern);
	}
	if(pos == std::string::npos)
		return nullptr;

	size_t start = pos + strlen(pattern);
	size_t end = html.find('"', start);
	if(end == std::string::npos || end <= start)
		return nullptr;

	std::string imgUrl = html.substr(start, end - start);
	size_t sPos = imgUrl.rfind("=s");
	if(sPos != std::string::npos)
	{
		size_t dash = imgUrl.find('-', sPos);
		if(dash != std::string::npos)
			imgUrl.replace(sPos, dash - sPos, "=s88");
	}

	std::string jpegData;
	if(!httpGetBinary(imgUrl.c_str(), jpegData, 5) || jpegData.empty())
		return nullptr;

	return decodeJpegToTexture((const uint8_t *)jpegData.data(), jpegData.size(), maxW, maxH, outW, outH);
}

static Thread gUpdateThread;
static volatile bool gUpdateAvailable = false;
static volatile bool gUpdateCheckStarted = false;

static bool isVersionNewer(const char *tag, const char *curVer)
{
	if(!tag || !curVer)
		return false;

	while(*tag == 'v' || *tag == 'V' || *tag == ' ')
		tag++;
	while(*curVer == 'v' || *curVer == 'V' || *curVer == ' ')
		curVer++;

	while(*tag != '\0' || *curVer != '\0')
	{
		while(*tag != '\0' && (*tag < '0' || *tag > '9'))
			tag++;
		while(*curVer != '\0' && (*curVer < '0' || *curVer > '9'))
			curVer++;

		if(*tag == '\0' && *curVer == '\0')
			break;

		int vTag = 0;
		while(*tag >= '0' && *tag <= '9')
		{
			vTag = vTag * 10 + (*tag - '0');
			tag++;
		}

		int vCur = 0;
		while(*curVer >= '0' && *curVer <= '9')
		{
			vCur = vCur * 10 + (*curVer - '0');
			curVer++;
		}

		if(vTag > vCur)
			return true;
		if(vTag < vCur)
			return false;
	}

	return false;
}

static void * updateCheckEntry(void * arg)
{
	(void)arg;
	if(!netReady())
		return nullptr;

	std::string response;
	CURL *curl = newCurl("https://api.github.com/repos/ReviveMii/brewtube/releases?per_page=5", &response);
	if(!curl)
		return nullptr;

	curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "User-Agent: BrewTube-Wii");
	headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);

	CURLcode res = curl_easy_perform(curl);
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if(res != CURLE_OK || httpCode != 200 || response.empty())
		return nullptr;

	JsonValue root;
	if(!jsonParse(response.data(), response.size(), root) || root.type != JsonType::Array)
		return nullptr;

	for(size_t i = 0; i < root.size(); i++)
	{
		const JsonValue *rel = root.at(i);
		if(!rel)
			continue;

		const JsonValue *tagVal = rel->get("tag_name");
		if(!tagVal)
			continue;

		const char *tag = tagVal->asString("");
		if(isVersionNewer(tag, "0.2"))
		{
			gUpdateAvailable = true;
			break;
		}
	}

	return nullptr;
}

void ytStartUpdateCheck()
{
	if(gUpdateCheckStarted)
		return;
	gUpdateCheckStarted = true;
	gUpdateThread.start(updateCheckEntry, nullptr, 64 * 1024, ThreadPriority::Low);
}

bool ytIsUpdateAvailable()
{
	return gUpdateAvailable;
}

