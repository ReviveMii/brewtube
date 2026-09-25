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

bool ytResolveStream(const char *videoId, char *urlOut, int urlOutSize, char *err, int errSize)
{
	std::string visitorData = getVisitorData();
	std::string body;
	body.reserve(2048);
	body += "{\"videoId\":\"";
	body += videoId;
	body += "\",\"context\":{\"client\":{\"clientName\":\"ANDROID\",\"clientVersion\":\"21.26.364\",\"androidSdkVersion\":30,\"userAgent\":\"com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip\",\"osName\":\"Android\",\"osVersion\":\"11\",\"hl\":\"en\",\"gl\":\"US\"";
	if(!visitorData.empty())
	{
		body += ",\"visitorData\":\"";
		body += jsonEscape(visitorData.c_str());
		body += "\"";
	}
	body += "}},\"contentCheckOk\":true,\"racyCheckOk\":true}";

	std::string response;
	const char *url = "https://www.youtube.com/youtubei/v1/player";

	if(!httpPost(url, body.c_str(), response))
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

	JPEGDEC jpeg;
	if(!jpeg.openRAM((uint8_t *)jpegData.data(), (int)jpegData.size(), jpegDrawCallback))
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
