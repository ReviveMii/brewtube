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
#include <unistd.h>
#include <string>
#include <vector>
#include <memory>

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
#include <png.h>

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

enum SearchType { SEARCH_VIDEO, SEARCH_CHANNEL, SEARCH_PLAYLIST };

struct SearchRenderer
{
	const JsonValue *node;
	SearchType type;
};

void collectSearchRenderers(const JsonValue &node, std::vector<SearchRenderer> &out, int maxResults, bool includeChannels = true)
{
	if((int)out.size() >= maxResults)
		return;

	if(node.type == JsonType::Object)
	{
		const JsonValue *vr = node.get("videoRenderer");
		if(vr && vr->get("videoId"))
		{
			out.push_back({vr, SEARCH_VIDEO});
			return;
		}
		const JsonValue *cvr = node.get("compactVideoRenderer");
		if(cvr && cvr->get("videoId"))
		{
			out.push_back({cvr, SEARCH_VIDEO});
			return;
		}
		if(includeChannels)
		{
			const JsonValue *cr = node.get("channelRenderer");
			if(cr && cr->get("channelId"))
			{
				out.push_back({cr, SEARCH_CHANNEL});
				return;
			}
		}
		const JsonValue *pr = node.get("playlistRenderer");
		if(!pr) pr = node.get("compactPlaylistRenderer");
		if(pr && pr->get("playlistId"))
		{
			out.push_back({pr, SEARCH_PLAYLIST});
			return;
		}
		for(const auto &kv : node.obj)
			collectSearchRenderers(kv.second, out, maxResults, includeChannels);
	}
	else if(node.type == JsonType::Array)
	{
		for(const auto &v : node.arr)
			collectSearchRenderers(v, out, maxResults, includeChannels);
	}
}

const JsonValue *findContinuationToken(const JsonValue &node)
{
	if(node.type == JsonType::Object)
	{
		const JsonValue *cir = node.get("continuationItemRenderer");
		if(cir)
		{
			const JsonValue *ep = cir->get("continuationEndpoint");
			const JsonValue *cc = ep ? ep->get("continuationCommand") : nullptr;
			const JsonValue *token = cc ? cc->get("token") : nullptr;
			if(token && token->asString("")[0] != '\0')
				return token;
		}

		for(const auto &kv : node.obj)
		{
			const JsonValue *found = findContinuationToken(kv.second);
			if(found)
				return found;
		}
	}
	else if(node.type == JsonType::Array)
	{
		for(const auto &v : node.arr)
		{
			const JsonValue *found = findContinuationToken(v);
			if(found)
				return found;
		}
	}
	return nullptr;
}

void extractChannelRendererAvatar(const JsonValue *cr, char *out, size_t outSize)
{
	out[0] = '\0';
	if(!cr) return;
	const JsonValue *thumbs = cr->get("thumbnail") ? cr->get("thumbnail")->get("thumbnails") : nullptr;
	if(thumbs && thumbs->size() > 0)
	{
		const JsonValue *u = thumbs->at(thumbs->size() - 1)->get("url");
		if(u)
		{
			const char *urlStr = u->asString("");
			if(strncmp(urlStr, "//", 2) == 0)
				snprintf(out, outSize, "https:%s", urlStr);
			else
				snprintf(out, outSize, "%s", urlStr);
		}
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

void extractAvatarUrl(const JsonValue *vr, char *out, size_t outSize)
{
	out[0] = '\0';
	if(!vr) return;
	const JsonValue *cts = vr->get("channelThumbnailSupportedRenderers");
	const JsonValue *ctlr = cts ? cts->get("channelThumbnailWithLinkRenderer") : nullptr;
	const JsonValue *thumb = ctlr ? ctlr->get("thumbnail") : nullptr;
	const JsonValue *thumbs = thumb ? thumb->get("thumbnails") : nullptr;
	if(!thumbs && vr->get("channelThumbnail"))
	{
		const JsonValue *ct = vr->get("channelThumbnail");
		thumbs = ct->get("thumbnails");
		if(!thumbs && ct->get("thumbnail"))
			thumbs = ct->get("thumbnail")->get("thumbnails");
	}
	if(thumbs && thumbs->size() > 0)
	{
		const JsonValue *u = thumbs->at(0)->get("url");
		if(u)
		{
			const char *urlStr = u->asString("");
			if(strncmp(urlStr, "//", 2) == 0)
				snprintf(out, outSize, "https:%s", urlStr);
			else
				snprintf(out, outSize, "%s", urlStr);
		}
	}
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

struct PngMemReader
{
	const uint8_t *data;
	size_t offset;
	size_t size;
};

static void pngReadMemCb(png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead)
{
	PngMemReader *r = static_cast<PngMemReader *>(png_get_io_ptr(png_ptr));
	if(r->offset + byteCountToRead <= r->size)
	{
		memcpy(outBytes, r->data + r->offset, byteCountToRead);
		r->offset += byteCountToRead;
	}
	else
	{
		size_t rem = r->size > r->offset ? r->size - r->offset : 0;
		if(rem > 0)
		{
			memcpy(outBytes, r->data + r->offset, rem);
			r->offset += rem;
		}
		memset(outBytes + rem, 0, byteCountToRead - rem);
	}
}

static bool decodePngToRgba(const uint8_t *data, size_t size, std::vector<uint8_t> &outRgba, int &outW, int &outH)
{
	if(!data || size < 8) return false;
	if(png_sig_cmp(static_cast<png_const_bytep>(data), 0, 8) != 0) return false;

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if(!png_ptr) return false;

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if(!info_ptr)
	{
		png_destroy_read_struct(&png_ptr, nullptr, nullptr);
		return false;
	}

	if(setjmp(png_jmpbuf(png_ptr)))
	{
		png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
		return false;
	}

	PngMemReader reader = { data, 0, size };
	png_set_read_fn(png_ptr, &reader, pngReadMemCb);
	png_read_info(png_ptr, info_ptr);

	png_uint_32 w = 0, h = 0;
	int bit_depth = 0, color_type = 0;
	png_get_IHDR(png_ptr, info_ptr, &w, &h, &bit_depth, &color_type, nullptr, nullptr, nullptr);

	if(bit_depth == 16) png_set_strip_16(png_ptr);
	if(color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
	if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
	if(png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
	if(color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
	if(color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);

	png_read_update_info(png_ptr, info_ptr);
	size_t rowBytes = png_get_rowbytes(png_ptr, info_ptr);

	outRgba.resize(rowBytes * h);
	std::vector<png_bytep> rowPointers(h);
	for(png_uint_32 i = 0; i < h; i++)
		rowPointers[i] = outRgba.data() + (size_t)i * rowBytes;

	png_read_image(png_ptr, rowPointers.data());
	png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

	outW = (int)w;
	outH = (int)h;
	return true;
}

void *decodeJpegToTexture(const uint8_t *data, size_t size, int maxW, int maxH, int *outW, int *outH)
{
	if(!data || size == 0)
		return nullptr;

	int srcW = 0, srcH = 0;
	std::vector<uint8_t> rgba;

	if(size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0)
	{
		if(!decodePngToRgba(data, size, rgba, srcW, srcH))
			return nullptr;
	}
	else
	{
		JPEGDEC jpeg;
		if(!jpeg.openRAM((uint8_t *)data, (int)size, jpegDrawCallback))
			return nullptr;

		srcW = jpeg.getWidth();
		srcH = jpeg.getHeight();
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
		rgba = std::move(ctx.rgba);
	}

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
		finalPixels = rgba.data();
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
				scaled[dIdx + 0] = rgba[sIdx + 0];
				scaled[dIdx + 1] = rgba[sIdx + 1];
				scaled[dIdx + 2] = rgba[sIdx + 2];
				scaled[dIdx + 3] = rgba[sIdx + 3];
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

int ytSearch(const char *query, YtResult *results, int maxResults, char *err, int errSize,
	const char *continuation, char *nextContinuationOut, int nextContinuationOutSize)
{
	std::string body = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260925.01.00\",\"hl\":\"en\",\"gl\":\"US\"}}";
	if(continuation && continuation[0])
	{
		body += ",\"continuation\":\"";
		body += jsonEscape(continuation);
		body += "\"";
	}
	else
	{
		body += ",\"query\":\"";
		body += jsonEscape(query);
		body += "\"";
	}
	body += "}";

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

	if(nextContinuationOut && nextContinuationOutSize > 0)
	{
		nextContinuationOut[0] = '\0';
		const JsonValue *token = findContinuationToken(root);
		if(token)
			snprintf(nextContinuationOut, nextContinuationOutSize, "%s", token->asString(""));
	}

	std::vector<SearchRenderer> renderers;
	collectSearchRenderers(root, renderers, maxResults);

	int count = 0;
	for(const SearchRenderer &sr : renderers)
	{
		if(count >= maxResults)
			break;

		const JsonValue *vr = sr.node;
		YtResult &r = results[count];
		r.isChannel = false;
		r.isPlaylist = false;
		r.playlistId[0] = '\0';

		if(sr.type == SEARCH_CHANNEL)
		{
			const char *cid = vr->get("channelId") ? vr->get("channelId")->asString("") : "";
			if(cid[0] == '\0')
				continue;

			r.isChannel = true;
			r.videoId[0] = '\0';
			snprintf(r.channelId, sizeof(r.channelId), "%s", cid);
			extractChannelRendererAvatar(vr, r.avatarUrl, sizeof(r.avatarUrl));
			snprintf(r.title, sizeof(r.title), "%s", firstRunText(vr->get("title"), "(channel)"));
			snprintf(r.author, sizeof(r.author), "%s", firstRunText(vr->get("subscriberCountText"), ""));
			snprintf(r.viewCountText, sizeof(r.viewCountText), "%s", firstRunText(vr->get("videoCountText"), ""));
			r.lengthText[0] = '\0';
			r.publishedText[0] = '\0';
			r.description[0] = '\0';
			count++;
			continue;
		}

		if(sr.type == SEARCH_PLAYLIST)
		{
			const char *pid = vr->get("playlistId") ? vr->get("playlistId")->asString("") : "";
			if(pid[0] == '\0')
				continue;

			r.isPlaylist = true;
			snprintf(r.videoId, sizeof(r.videoId), "%s", pid);
			snprintf(r.playlistId, sizeof(r.playlistId), "%s", pid);
			r.channelId[0] = '\0';
			snprintf(r.title, sizeof(r.title), "%s", firstRunText(vr->get("title"), "(playlist)"));
			const char *author = firstRunText(vr->get("longBylineText"), "");
			if(!author || author[0] == '\0') author = firstRunText(vr->get("shortBylineText"), "");
			snprintf(r.author, sizeof(r.author), "%s", author ? author : "");
			snprintf(r.lengthText, sizeof(r.lengthText), "%s", firstRunText(vr->get("videoCount"), "Playlist"));
			r.viewCountText[0] = '\0';
			r.publishedText[0] = '\0';
			r.description[0] = '\0';
			extractAvatarUrl(vr, r.avatarUrl, sizeof(r.avatarUrl));
			count++;
			continue;
		}

		const char *vid = vr->get("videoId") ? vr->get("videoId")->asString("") : "";
		snprintf(r.videoId, sizeof(r.videoId), "%s", vid);
		if(r.videoId[0] == '\0')
			continue;

		snprintf(r.channelId, sizeof(r.channelId), "%s", extractChannelId(vr));
		extractAvatarUrl(vr, r.avatarUrl, sizeof(r.avatarUrl));
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
		r.description[0] = '\0';
		count++;
	}

	if(count == 0)
		snprintf(err, errSize, "No results found");

	return count;
}

void collectSuggestions(const JsonValue &node, YtResult *results, int &count, int maxResults)
{
	if(count >= maxResults)
		return;

	if(node.type == JsonType::Object)
	{
		const JsonValue *vr = node.get("videoRenderer");
		if(!vr) vr = node.get("compactVideoRenderer");
		if(vr && vr->get("videoId"))
		{
			const char *vid = vr->get("videoId")->asString("");
			if(vid[0] != '\0')
			{
				YtResult &r = results[count];
				memset(&r, 0, sizeof(r));
				r.isChannel = false;
				snprintf(r.videoId, sizeof(r.videoId), "%s", vid);
				snprintf(r.channelId, sizeof(r.channelId), "%s", extractChannelId(vr));
				extractAvatarUrl(vr, r.avatarUrl, sizeof(r.avatarUrl));
				snprintf(r.title, sizeof(r.title), "%s", firstRunText(vr->get("title"), "(untitled)"));

				const char *author = firstRunText(vr->get("shortBylineText"), "");
				if(!author || author[0] == '\0')
					author = firstRunText(vr->get("longBylineText"), "");
				snprintf(r.author, sizeof(r.author), "%s", author ? author : "");

				snprintf(r.lengthText, sizeof(r.lengthText), "%s", firstRunText(vr->get("lengthText"), "Live"));
				const char *views = firstRunText(vr->get("shortViewCountText"), "");
				if(!views || views[0] == '\0')
					views = firstRunText(vr->get("viewCountText"), "");
				snprintf(r.viewCountText, sizeof(r.viewCountText), "%s", views ? views : "");
				snprintf(r.publishedText, sizeof(r.publishedText), "%s", firstRunText(vr->get("publishedTimeText"), ""));
				count++;
				if(count >= maxResults) return;
			}
		}

		const JsonValue *lvm = node.get("lockupViewModel");
		if(lvm)
		{
			const char *ctype = lvm->get("contentType") ? lvm->get("contentType")->asString("") : "";
			const char *cid = lvm->get("contentId") ? lvm->get("contentId")->asString("") : "";
			if((ctype[0] == '\0' || strcmp(ctype, "LOCKUP_CONTENT_TYPE_VIDEO") == 0) &&
			   cid[0] != '\0' && strlen(cid) == 11 && strncmp(cid, "RD", 2) != 0)
			{
				const JsonValue *meta = lvm->get("metadata");
				const JsonValue *lmvm = meta ? meta->get("lockupMetadataViewModel") : nullptr;
				const JsonValue *titleVal = lmvm && lmvm->get("title") ? lmvm->get("title")->get("content") : nullptr;
				const char *title = titleVal ? titleVal->asString("") : "";

				if(title[0] != '\0')
				{
					YtResult &r = results[count];
					memset(&r, 0, sizeof(r));
					r.isChannel = false;
					snprintf(r.videoId, sizeof(r.videoId), "%s", cid);
					snprintf(r.title, sizeof(r.title), "%s", title);

					const JsonValue *cmvm = lmvm->get("metadata") ? lmvm->get("metadata")->get("contentMetadataViewModel") : nullptr;
					const JsonValue *rows = cmvm ? cmvm->get("metadataRows") : nullptr;
					if(rows && rows->type == JsonType::Array)
					{
						if(rows->arr.size() > 0 && rows->arr[0].type == JsonType::Object)
						{
							const JsonValue *parts = rows->arr[0].get("metadataParts");
							if(parts && parts->type == JsonType::Array && parts->arr.size() > 0)
							{
								const JsonValue *txt = parts->arr[0].get("text") ? parts->arr[0].get("text")->get("content") : nullptr;
								if(txt) snprintf(r.author, sizeof(r.author), "%s", txt->asString(""));
							}
						}
						if(rows->arr.size() > 1 && rows->arr[1].type == JsonType::Object)
						{
							const JsonValue *parts = rows->arr[1].get("metadataParts");
							if(parts && parts->type == JsonType::Array)
							{
								if(parts->arr.size() > 0)
								{
									const JsonValue *txt = parts->arr[0].get("text") ? parts->arr[0].get("text")->get("content") : nullptr;
									if(txt) snprintf(r.viewCountText, sizeof(r.viewCountText), "%s", txt->asString(""));
								}
								if(parts->arr.size() > 1)
								{
									const JsonValue *txt = parts->arr[1].get("text") ? parts->arr[1].get("text")->get("content") : nullptr;
									if(txt) snprintf(r.publishedText, sizeof(r.publishedText), "%s", txt->asString(""));
								}
							}
						}
					}

					const JsonValue *cimg = lvm->get("contentImage");
					const JsonValue *tvm = cimg ? cimg->get("thumbnailViewModel") : nullptr;
					if(tvm)
					{
						const JsonValue *overlays = tvm->get("overlays");
						if(overlays && overlays->type == JsonType::Array)
						{
							for(const auto &ov : overlays->arr)
							{
								const JsonValue *tbovm = ov.get("thumbnailBottomOverlayViewModel");
								const JsonValue *badges = tbovm ? tbovm->get("badges") : nullptr;
								if(badges && badges->type == JsonType::Array && badges->arr.size() > 0)
								{
									const JsonValue *tbvm = badges->arr[0].get("thumbnailBadgeViewModel");
									const JsonValue *txt = tbvm ? tbvm->get("text") : nullptr;
									if(txt && txt->asString("")[0] != '\0')
									{
										snprintf(r.lengthText, sizeof(r.lengthText), "%s", txt->asString(""));
										break;
									}
								}
							}
						}
					}

					const JsonValue *davm = lmvm ? lmvm->get("image") : nullptr;
					if(davm) davm = davm->get("decoratedAvatarViewModel");
					if(davm)
					{
						const JsonValue *avm = davm->get("avatar") ? davm->get("avatar")->get("avatarViewModel") : nullptr;
						const JsonValue *asrcs = avm && avm->get("image") ? avm->get("image")->get("sources") : nullptr;
						if(asrcs && asrcs->type == JsonType::Array && asrcs->arr.size() > 0)
						{
							const JsonValue *aurl = asrcs->arr[0].get("url");
							if(aurl) snprintf(r.avatarUrl, sizeof(r.avatarUrl), "%s", aurl->asString(""));
						}

						const JsonValue *rc = davm->get("rendererContext") ? davm->get("rendererContext")->get("commandContext") : nullptr;
						const JsonValue *onTap = rc ? rc->get("onTap") : nullptr;
						const JsonValue *cmd = onTap ? onTap->get("innertubeCommand") : nullptr;
						const JsonValue *be = cmd ? cmd->get("browseEndpoint") : nullptr;
						const JsonValue *bid = be ? be->get("browseId") : nullptr;
						if(bid) snprintf(r.channelId, sizeof(r.channelId), "%s", bid->asString(""));
					}

					if(r.lengthText[0] == '\0')
						snprintf(r.lengthText, sizeof(r.lengthText), "Live");

					count++;
					if(count >= maxResults) return;
				}
			}
		}

		for(const auto &kv : node.obj)
		{
			collectSuggestions(kv.second, results, count, maxResults);
			if(count >= maxResults) return;
		}
	}
	else if(node.type == JsonType::Array)
	{
		for(const auto &v : node.arr)
		{
			collectSuggestions(v, results, count, maxResults);
			if(count >= maxResults) return;
		}
	}
}

int ytGetSuggestions(const char *videoId, YtResult *results, int maxResults, char *err, int errSize)
{
	std::string body = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260925.01.00\",\"hl\":\"en\",\"gl\":\"US\"}},\"videoId\":\"";
	body += jsonEscape(videoId);
	body += "\"}";

	std::string response;
	if(!httpPost("https://www.youtube.com/youtubei/v1/next", body.c_str(), response))
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

	int count = 0;
	collectSuggestions(root, results, count, maxResults);

	if(count == 0)
		snprintf(err, errSize, "No suggestions found");

	return count;
}

bool ytFetchVoteData(const char *videoId, YtVoteData *voteOut, char *err, int errSize)
{
	char url[160];
	snprintf(url, sizeof(url), "https://returnyoutubedislikeapi.com/votes?videoId=%s", videoId);

	std::string response;
	if(!httpGetBinary(url, response, 6))
	{
		snprintf(err, errSize, "Could not reach vote service");
		return false;
	}

	JsonValue root;
	if(!jsonParse(response.c_str(), response.size(), root))
	{
		snprintf(err, errSize, "Unexpected response from vote service");
		return false;
	}

	const JsonValue *likes = root.get("likes");
	const JsonValue *dislikes = root.get("dislikes");
	if(!likes || !dislikes)
	{
		snprintf(err, errSize, "No vote data for this video");
		return false;
	}

	voteOut->likes = likes->asInt(0);
	voteOut->dislikes = dislikes->asInt(0);
	return true;
}

static YtClientType gClientType = YT_CLIENT_ANDROID;
static int gVolume = 200;
static bool gCaptionsEnabled = false;
static std::vector<YtSubscription> gSubscriptions;
static std::vector<YtLocalPlaylist> gLocalPlaylists;
static bool gPrefsLoaded = false;
static Mutex & getPrefsLock()
{
	static Mutex lock;
	return lock;
}

void prefsLoad();
void prefsSave();

void ytSetClient(YtClientType client)
{
	prefsLoad();
	getPrefsLock().lock();
	gClientType = client;
	prefsSave();
	getPrefsLock().unlock();
}

YtClientType ytGetClient()
{
	prefsLoad();
	return gClientType;
}

int ytGetVolume()
{
	prefsLoad();
	return gVolume;
}

void ytSetVolume(int vol)
{
	prefsLoad();
	if(vol < 0) vol = 0;
	if(vol > 255) vol = 255;
	getPrefsLock().lock();
	if(gVolume != vol)
	{
		gVolume = vol;
		prefsSave();
	}
	getPrefsLock().unlock();
}

bool ytGetCaptionsEnabled()
{
	prefsLoad();
	return gCaptionsEnabled;
}

void ytSetCaptionsEnabled(bool enabled)
{
	prefsLoad();
	getPrefsLock().lock();
	if(gCaptionsEnabled != enabled)
	{
		gCaptionsEnabled = enabled;
		prefsSave();
	}
	getPrefsLock().unlock();
}

static YtCaptionTrackList sLastCaptionTracks;

bool ytResolveStream(const char *videoId, char *urlOut, int urlOutSize, char *err, int errSize, YtResult *infoOut)
{
	std::string visitorData = getVisitorData();
	const char *playerUrl = "https://www.youtube.com/youtubei/v1/player";

	auto extractMeta = [&](const JsonValue & root) {
		const JsonValue *caps = root.get("captions");
		const JsonValue *pctr = caps ? caps->get("playerCaptionsTracklistRenderer") : nullptr;
		const JsonValue *tracks = pctr ? pctr->get("captionTracks") : nullptr;
		if(tracks && tracks->type == JsonType::Array && tracks->arr.size() > 0)
		{
			sLastCaptionTracks.count = 0;
			for(const auto &t : tracks->arr)
			{
				if(sLastCaptionTracks.count >= YT_MAX_CAPTION_TRACKS)
					break;
				const JsonValue *url = t.get("baseUrl");
				if(!url || url->asString("")[0] == '\0')
					continue;
				YtCaptionTrack &ct = sLastCaptionTracks.tracks[sLastCaptionTracks.count];
				snprintf(ct.baseUrl, sizeof(ct.baseUrl), "%s", url->asString(""));
				const JsonValue *lang = t.get("languageCode");
				snprintf(ct.languageCode, sizeof(ct.languageCode), "%s", lang ? lang->asString("") : "");
				const char *tname = firstRunText(t.get("name"), ct.languageCode[0] ? ct.languageCode : "Unknown");
				snprintf(ct.name, sizeof(ct.name), "%s", tname);
				sLastCaptionTracks.count++;
			}
		}

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
			if(infoOut->description[0] == '\0' && vd->get("shortDescription"))
				snprintf(infoOut->description, sizeof(infoOut->description), "%s", vd->get("shortDescription")->asString(""));
			if(infoOut->lengthText[0] == '\0' && vd->get("lengthSeconds"))
			{
				int s = atoi(vd->get("lengthSeconds")->asString("0"));
				if(s > 0)
				{
					int h = s / 3600;
					int m = (s % 3600) / 60;
					int sec = s % 60;
					if(h > 0)
						snprintf(infoOut->lengthText, sizeof(infoOut->lengthText), "%d:%02d:%02d", h, m, sec);
					else
						snprintf(infoOut->lengthText, sizeof(infoOut->lengthText), "%02d:%02d", m, sec);
				}
			}
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
			if(!fmt)
				continue;
			const JsonValue *it = fmt->get("itag");
			if(!it || it->asInt(-1) != 18)
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

bool ytGetCaptionTracks(YtCaptionTrackList *out)
{
	if(!out)
		return false;
	*out = sLastCaptionTracks;
	return out->count > 0;
}

bool ytFetchCaptions(const char *url, std::vector<YtCaptionLine> &linesOut)
{
	linesOut.clear();
	if(!url || url[0] == '\0')
		return false;

	std::string xml;
	if(!httpGetBinary(url, xml, 6) || xml.empty())
		return false;

	size_t pos = 0;
	while(true)
	{
		size_t pStart = xml.find("<p ", pos);
		if(pStart == std::string::npos)
			break;
		size_t tagEnd = xml.find('>', pStart);
		if(tagEnd == std::string::npos)
			break;

		std::string tagAttr = xml.substr(pStart + 3, tagEnd - (pStart + 3));
		uint32_t startMs = 0;
		uint32_t durationMs = 0;

		size_t tPos = tagAttr.find("t=\"");
		if(tPos != std::string::npos)
			startMs = (uint32_t)strtoul(tagAttr.c_str() + tPos + 3, nullptr, 10);

		size_t dPos = tagAttr.find("d=\"");
		if(dPos != std::string::npos)
			durationMs = (uint32_t)strtoul(tagAttr.c_str() + dPos + 3, nullptr, 10);

		size_t contentEnd = xml.find("</p>", tagEnd);
		if(contentEnd == std::string::npos)
			contentEnd = xml.find('<', tagEnd + 1);
		if(contentEnd == std::string::npos)
			contentEnd = xml.size();

		std::string raw = xml.substr(tagEnd + 1, contentEnd - (tagEnd + 1));
		pos = contentEnd;

		std::string clean;
		clean.reserve(raw.size());
		for(size_t i = 0; i < raw.size(); i++)
		{
			if(raw[i] == '<')
			{
				if(raw.compare(i, 4, "<br>") == 0 || raw.compare(i, 5, "<br/>") == 0 || raw.compare(i, 6, "<br />") == 0)
				{
					clean += '\n';
				}
				size_t closeTag = raw.find('>', i);
				if(closeTag != std::string::npos)
					i = closeTag;
				continue;
			}

			if(raw[i] == '&')
			{
				size_t semi = raw.find(';', i);
				if(semi != std::string::npos && semi - i <= 10)
				{
					if(raw[i + 1] == '#')
					{
						unsigned long code = 0;
						if(raw[i + 2] == 'x' || raw[i + 2] == 'X')
							code = strtoul(raw.c_str() + i + 3, nullptr, 16);
						else
							code = strtoul(raw.c_str() + i + 2, nullptr, 10);

						if(code == 10 || code == 13)
						{
							clean += '\n';
						}
						else if(code == 160)
						{
							clean += ' ';
						}
						else if(code < 0x80)
						{
							clean += (char)code;
						}
						else if(code < 0x800)
						{
							clean += (char)(0xC0 | (code >> 6));
							clean += (char)(0x80 | (code & 0x3F));
						}
						else if(code < 0x10000)
						{
							clean += (char)(0xE0 | (code >> 12));
							clean += (char)(0x80 | ((code >> 6) & 0x3F));
							clean += (char)(0x80 | (code & 0x3F));
						}
						else if(code < 0x110000)
						{
							clean += (char)(0xF0 | (code >> 18));
							clean += (char)(0x80 | ((code >> 12) & 0x3F));
							clean += (char)(0x80 | ((code >> 6) & 0x3F));
							clean += (char)(0x80 | (code & 0x3F));
						}
						i = semi;
						continue;
					}
					else
					{
						std::string ent = raw.substr(i, semi - i + 1);
						if(ent == "&quot;") { clean += '"'; i = semi; continue; }
						if(ent == "&apos;") { clean += '\''; i = semi; continue; }
						if(ent == "&amp;") { clean += '&'; i = semi; continue; }
						if(ent == "&lt;") { clean += '<'; i = semi; continue; }
						if(ent == "&gt;") { clean += '>'; i = semi; continue; }
						if(ent == "&nbsp;") { clean += ' '; i = semi; continue; }
					}
				}
			}

			if(raw[i] == '\r')
				continue;

			clean += raw[i];
		}

		while(!clean.empty() && (clean.front() == ' ' || clean.front() == '\n' || clean.front() == '\t'))
			clean.erase(clean.begin());
		while(!clean.empty() && (clean.back() == ' ' || clean.back() == '\n' || clean.back() == '\t'))
			clean.pop_back();

		std::string normalized;
		normalized.reserve(clean.size());
		bool prevNewline = false;
		for(size_t ci = 0; ci < clean.size(); ci++)
		{
			if(clean[ci] == '\n')
			{
				if(prevNewline) continue;
				prevNewline = true;
			}
			else
			{
				prevNewline = false;
			}
			normalized += clean[ci];
		}
		clean = normalized;

		if(!clean.empty())
		{
			YtCaptionLine line;
			line.startMs = startMs;
			line.endMs = startMs + (durationMs > 0 ? durationMs : 2500);
			snprintf(line.text, sizeof(line.text), "%s", clean.c_str());
			linesOut.push_back(line);
		}
	}

	return !linesOut.empty();
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

void *ytFetchChannelPfp(const char *channelIdOrUrl, int maxW, int maxH, int *outW, int *outH)
{
	if(!channelIdOrUrl || channelIdOrUrl[0] == '\0')
		return nullptr;

	std::string imgUrl;
	if(strncmp(channelIdOrUrl, "http://", 7) == 0 || strncmp(channelIdOrUrl, "https://", 8) == 0)
	{
		imgUrl = channelIdOrUrl;
	}
	else
	{
		std::string url = "https://www.youtube.com/channel/";
		url += channelIdOrUrl;

		std::string html;
		CURL *curl = newCurl(url.c_str(), &html);
		if(!curl)
			return nullptr;

		curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
		curl_easy_setopt(curl, CURLOPT_COOKIE, "SOCS=CAISNQgDEitib3FfaWRlbnRpdHlmcm9udGVuZHVpc2VydmVyXzIwMjYwNzE0LjA3X3AwGgJkZSACGgYIgL7g0gY; PREF=f6=40000000&tz=Europe.Berlin");
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);

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

		imgUrl = html.substr(start, end - start);
	}
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
	usleep(2000000);
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
		if(isVersionNewer(tag, "0.4"))
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

void *ytFetchImage(const char *url, int maxW, int maxH, int *outW, int *outH)
{
	if(!url || url[0] == '\0')
		return nullptr;

	std::string fullUrl;
	if(strncmp(url, "//", 2) == 0)
	{
		fullUrl = "https:";
		fullUrl += url;
		url = fullUrl.c_str();
	}

	std::string jpegData;
	if(!httpGetBinary(url, jpegData, 5) || jpegData.empty())
		return nullptr;

	return decodeJpegToTexture((const uint8_t *)jpegData.data(), jpegData.size(), maxW, maxH, outW, outH);
}

bool ytChannelBrowse(const char *channelIdOrHandle, YtChannelDetails *details, YtChannelTab tab, YtChannelFilter filter, YtChannelItem *items, int maxItems, int *outCount, char *err, int errSize)
{
	std::string browseId;
	if(!channelIdOrHandle || channelIdOrHandle[0] == '\0')
	{
		snprintf(err, errSize, "Invalid channel identifier");
		return false;
	}

	if(channelIdOrHandle[0] == '@' || (strncmp(channelIdOrHandle, "UC", 2) != 0 && strchr(channelIdOrHandle, '/') == nullptr))
	{
		std::string navUrl = "https://www.youtube.com/";
		if(channelIdOrHandle[0] != '@')
			navUrl += "@";
		navUrl += channelIdOrHandle;

		std::string resolveBody = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260925.01.00\",\"hl\":\"en\",\"gl\":\"US\"}},\"url\":\"";
		resolveBody += jsonEscape(navUrl.c_str());
		resolveBody += "\"}";

		std::string resolveResp;
		if(httpPost("https://www.youtube.com/youtubei/v1/navigation/resolve_url", resolveBody.c_str(), resolveResp))
		{
			JsonValue rRoot;
			if(jsonParse(resolveResp.c_str(), resolveResp.size(), rRoot))
			{
				const JsonValue *ep = rRoot.get("endpoint");
				const JsonValue *bp = ep ? ep->get("browseEndpoint") : nullptr;
				if(bp && bp->get("browseId"))
					browseId = bp->get("browseId")->asString("");
			}
		}
	}

	if(browseId.empty())
		browseId = channelIdOrHandle;

	std::string body;
	body.reserve(1024);

	if(tab == YT_CHAN_TAB_VIDEOS && filter == YT_CHAN_FILTER_POPULAR && details && details->popularToken[0])
	{
		body = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260925.01.00\",\"hl\":\"en\",\"gl\":\"US\"}},\"continuation\":\"";
		body += jsonEscape(details->popularToken);
		body += "\"}";
	}
	else if(tab == YT_CHAN_TAB_VIDEOS && filter == YT_CHAN_FILTER_OLDEST && details && details->oldestToken[0])
	{
		body = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260925.01.00\",\"hl\":\"en\",\"gl\":\"US\"}},\"continuation\":\"";
		body += jsonEscape(details->oldestToken);
		body += "\"}";
	}
	else
	{
		body = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260925.01.00\",\"hl\":\"en\",\"gl\":\"US\"}},\"browseId\":\"";
		body += jsonEscape(browseId.c_str());
		body += "\"";

		const char *params = nullptr;
		if(tab == YT_CHAN_TAB_VIDEOS)
			params = "EgZ2aWRlb3PyBgQKAjoA";
		else if(tab == YT_CHAN_TAB_SHORTS)
			params = "EgZzaG9ydHPyBgUKA5oBAA%3D%3D";
		else if(tab == YT_CHAN_TAB_PLAYLISTS)
			params = "EglwbGF5bGlzdHPyBgoKCEIGCgIQaCIA";
		else if(tab == YT_CHAN_TAB_POSTS)
			params = "EgVwb3N0c_IGBAoCSgA%3D";

		if(params)
		{
			body += ",\"params\":\"";
			body += params;
			body += "\"";
		}
		body += "}";
	}

	std::string response;
	if(!httpPost("https://www.youtube.com/youtubei/v1/browse", body.c_str(), response))
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

	if(details)
	{
		snprintf(details->channelId, sizeof(details->channelId), "%s", browseId.c_str());

		const JsonValue *hdr = root.get("header");
		const JsonValue *ph = hdr ? hdr->get("pageHeaderRenderer") : nullptr;
		const JsonValue *vm = ph ? (ph->get("content") ? ph->get("content")->get("pageHeaderViewModel") : nullptr) : nullptr;

		if(vm)
		{
			const JsonValue *t = vm->get("title") ? vm->get("title")->get("dynamicTextViewModel") : nullptr;
			if(t && t->get("text") && t->get("text")->get("content"))
				snprintf(details->title, sizeof(details->title), "%s", t->get("text")->get("content")->asString(""));

			const JsonValue *meta = vm->get("metadata") ? vm->get("metadata")->get("contentMetadataViewModel") : nullptr;
			const JsonValue *rows = meta ? meta->get("metadataRows") : nullptr;
			if(rows && rows->size() > 0)
			{
				const JsonValue *r0 = rows->at(0)->get("metadataParts");
				if(r0 && r0->size() > 0 && r0->at(0)->get("text") && r0->at(0)->get("text")->get("content"))
					snprintf(details->handle, sizeof(details->handle), "%s", r0->at(0)->get("text")->get("content")->asString(""));

				if(rows->size() > 1)
				{
					const JsonValue *r1 = rows->at(1)->get("metadataParts");
					if(r1 && r1->size() > 0 && r1->at(0)->get("text") && r1->at(0)->get("text")->get("content"))
						snprintf(details->subscriberCount, sizeof(details->subscriberCount), "%s", r1->at(0)->get("text")->get("content")->asString(""));
					if(r1 && r1->size() > 1 && r1->at(1)->get("text") && r1->at(1)->get("text")->get("content"))
						snprintf(details->videoCount, sizeof(details->videoCount), "%s", r1->at(1)->get("text")->get("content")->asString(""));
				}
			}

			const JsonValue *img = vm->get("image") ? vm->get("image")->get("decoratedAvatarViewModel") : nullptr;
			const JsonValue *avm = img ? img->get("avatar") : nullptr;
			const JsonValue *avvm = avm ? avm->get("avatarViewModel") : nullptr;
			const JsonValue *srcs = avvm && avvm->get("image") ? avvm->get("image")->get("sources") : nullptr;
			if(!srcs && vm->get("animatedImage"))
			{
				const JsonValue *cp = vm->get("animatedImage")->get("contentPreviewImageViewModel");
				if(cp && cp->get("image"))
					srcs = cp->get("image")->get("sources");
			}
			if(!srcs && vm->get("image") && vm->get("image")->get("contentPreviewImageViewModel"))
			{
				const JsonValue *cp = vm->get("image")->get("contentPreviewImageViewModel");
				if(cp && cp->get("image"))
					srcs = cp->get("image")->get("sources");
			}
			if(srcs && srcs->size() > 0 && srcs->at(0)->get("url"))
			{
				const char *u = srcs->at(0)->get("url")->asString("");
				if(strncmp(u, "//", 2) == 0)
					snprintf(details->avatarUrl, sizeof(details->avatarUrl), "https:%s", u);
				else
					snprintf(details->avatarUrl, sizeof(details->avatarUrl), "%s", u);
			}

			const JsonValue *ban = vm->get("banner") ? vm->get("banner")->get("imageBannerViewModel") : nullptr;
			const JsonValue *bsrcs = ban && ban->get("image") ? ban->get("image")->get("sources") : nullptr;
			if(bsrcs && bsrcs->size() > 0 && bsrcs->at(0)->get("url"))
			{
				const char *u = bsrcs->at(0)->get("url")->asString("");
				if(strncmp(u, "//", 2) == 0)
					snprintf(details->bannerUrl, sizeof(details->bannerUrl), "https:%s", u);
				else
					snprintf(details->bannerUrl, sizeof(details->bannerUrl), "%s", u);
			}

			const JsonValue *descObj = vm->get("description") ? vm->get("description")->get("descriptionPreviewViewModel") : nullptr;
			if(descObj && descObj->get("description") && descObj->get("description")->get("content"))
				snprintf(details->description, sizeof(details->description), "%s", descObj->get("description")->get("content")->asString(""));
		}
		else
		{
			const JsonValue *c4 = hdr ? hdr->get("c4TabbedHeaderRenderer") : nullptr;
			if(c4)
			{
				if(c4->get("title"))
					snprintf(details->title, sizeof(details->title), "%s", c4->get("title")->asString(""));
				if(c4->get("subscriberCountText"))
					snprintf(details->subscriberCount, sizeof(details->subscriberCount), "%s", firstRunText(c4->get("subscriberCountText"), ""));

				const JsonValue *av = c4->get("avatar") ? c4->get("avatar")->get("thumbnails") : nullptr;
				if(av && av->size() > 0 && av->at(0)->get("url"))
				{
					const char *u = av->at(0)->get("url")->asString("");
					if(strncmp(u, "//", 2) == 0)
						snprintf(details->avatarUrl, sizeof(details->avatarUrl), "https:%s", u);
					else
						snprintf(details->avatarUrl, sizeof(details->avatarUrl), "%s", u);
				}

				const JsonValue *ban = c4->get("banner") ? c4->get("banner")->get("thumbnails") : nullptr;
				if(ban && ban->size() > 0 && ban->at(0)->get("url"))
				{
					const char *u = ban->at(0)->get("url")->asString("");
					if(strncmp(u, "//", 2) == 0)
						snprintf(details->bannerUrl, sizeof(details->bannerUrl), "https:%s", u);
					else
						snprintf(details->bannerUrl, sizeof(details->bannerUrl), "%s", u);
				}
			}
		}

		const JsonValue *tabsNode = root.get("contents") ? root.get("contents")->get("twoColumnBrowseResultsRenderer") : nullptr;
		const JsonValue *tabsArr = tabsNode ? tabsNode->get("tabs") : nullptr;
		if(tabsArr)
		{
			for(size_t t = 0; t < tabsArr->size(); t++)
			{
				const JsonValue *tr = tabsArr->at(t)->get("tabRenderer");
				const JsonValue *cnt = tr ? tr->get("content") : nullptr;
				const JsonValue *rg = cnt ? cnt->get("richGridRenderer") : nullptr;
				const JsonValue *h = rg ? rg->get("header") : nullptr;
				const JsonValue *cb = h ? h->get("chipBarViewModel") : nullptr;
				const JsonValue *chips = cb ? cb->get("chips") : nullptr;
				if(chips)
				{
					for(size_t c = 0; c < chips->size(); c++)
					{
						const JsonValue *cvm = chips->at(c)->get("chipViewModel");
						if(!cvm) continue;
						const char *txt = cvm->get("text") ? cvm->get("text")->asString("") : "";
						const JsonValue *tc = cvm->get("tapCommand") ? cvm->get("tapCommand")->get("innertubeCommand") : nullptr;
						const JsonValue *cc = tc ? tc->get("continuationCommand") : nullptr;
						const char *tok = cc && cc->get("token") ? cc->get("token")->asString("") : "";
						if(tok[0])
						{
							if(strcasecmp(txt, "Latest") == 0 || strcasecmp(txt, "Newest") == 0)
								snprintf(details->latestToken, sizeof(details->latestToken), "%s", tok);
							else if(strcasecmp(txt, "Popular") == 0)
								snprintf(details->popularToken, sizeof(details->popularToken), "%s", tok);
							else if(strcasecmp(txt, "Oldest") == 0)
								snprintf(details->oldestToken, sizeof(details->oldestToken), "%s", tok);
						}
					}
				}
			}
		}
	}

	int count = 0;
	if(tab == YT_CHAN_TAB_ABOUT)
	{
		if(outCount) *outCount = 0;
		return true;
	}

	std::vector<const JsonValue *> rawNodes;
	const JsonValue *acts = root.get("onResponseReceivedActions");
	if(acts && acts->type == JsonType::Array)
	{
		for(size_t a = 0; a < acts->size(); a++)
		{
			const JsonValue *act = acts->at(a);
			const JsonValue *appendAct = act ? act->get("appendContinuationItemsAction") : nullptr;
			if(!appendAct) appendAct = act ? act->get("reloadContinuationItemsCommand") : nullptr;
			const JsonValue *cItems = appendAct ? appendAct->get("continuationItems") : nullptr;
			if(cItems && cItems->type == JsonType::Array)
			{
				for(size_t ci = 0; ci < cItems->size(); ci++)
					rawNodes.push_back(cItems->at(ci));
			}
		}
	}
	else
	{
		const JsonValue *tabsNode = root.get("contents") ? root.get("contents")->get("twoColumnBrowseResultsRenderer") : nullptr;
		const JsonValue *tabsArr = tabsNode ? tabsNode->get("tabs") : nullptr;
		if(tabsArr)
		{
			for(size_t t = 0; t < tabsArr->size(); t++)
			{
				const JsonValue *tr = tabsArr->at(t)->get("tabRenderer");
				if(!tr) continue;
				if(tr->get("selected") && tr->get("selected")->asBool(false))
				{
					const JsonValue *cnt = tr->get("content");
					if(!cnt) continue;
					const JsonValue *rg = cnt->get("richGridRenderer");
					if(rg && rg->get("contents"))
					{
						const JsonValue *rgc = rg->get("contents");
						for(size_t i = 0; i < rgc->size(); i++)
							rawNodes.push_back(rgc->at(i));
					}
					else if(cnt->get("sectionListRenderer"))
					{
						const JsonValue *sl = cnt->get("sectionListRenderer")->get("contents");
						if(sl)
						{
							for(size_t si = 0; si < sl->size(); si++)
							{
								const JsonValue *isr = sl->at(si)->get("itemSectionRenderer");
								const JsonValue *isrc = isr ? isr->get("contents") : nullptr;
								if(isrc)
								{
									for(size_t ii = 0; ii < isrc->size(); ii++)
										rawNodes.push_back(isrc->at(ii));
								}
							}
						}
					}
				}
			}
		}
	}

	for(const JsonValue *node : rawNodes)
	{
		if(count >= maxItems)
			break;

		if(node->get("richItemRenderer"))
			node = node->get("richItemRenderer")->get("content");
		if(!node)
			continue;

		YtChannelItem &item = items[count];
		item.id[0] = '\0';
		item.title[0] = '\0';
		item.duration[0] = '\0';
		item.views[0] = '\0';
		item.date[0] = '\0';
		item.thumbUrl[0] = '\0';
		item.isPlayable = false;

		const JsonValue *lvm = node->get("lockupViewModel");
		if(lvm)
		{
			const JsonValue *lmv = lvm->get("metadata") ? lvm->get("metadata")->get("lockupMetadataViewModel") : nullptr;
			if(lmv)
			{
				if(lmv->get("title") && lmv->get("title")->get("content"))
					snprintf(item.title, sizeof(item.title), "%s", lmv->get("title")->get("content")->asString(""));

				const JsonValue *cmv = lmv->get("metadata") ? lmv->get("metadata")->get("contentMetadataViewModel") : nullptr;
				const JsonValue *rows = cmv ? cmv->get("metadataRows") : nullptr;
				if(rows && rows->size() > 0)
				{
					const JsonValue *parts = rows->at(0)->get("metadataParts");
					if(parts && parts->size() > 0 && parts->at(0)->get("text") && parts->at(0)->get("text")->get("content"))
						snprintf(item.views, sizeof(item.views), "%s", parts->at(0)->get("text")->get("content")->asString(""));
					if(parts && parts->size() > 1 && parts->at(1)->get("text") && parts->at(1)->get("text")->get("content"))
						snprintf(item.date, sizeof(item.date), "%s", parts->at(1)->get("text")->get("content")->asString(""));
				}
			}

			const JsonValue *rc = lvm->get("rendererContext") ? lvm->get("rendererContext")->get("commandContext") : nullptr;
			const JsonValue *onTap = rc ? rc->get("onTap") : nullptr;
			const JsonValue *cmd = onTap ? onTap->get("innertubeCommand") : nullptr;
			const JsonValue *we = cmd ? cmd->get("watchEndpoint") : nullptr;
			if(we && we->get("videoId"))
			{
				snprintf(item.id, sizeof(item.id), "%s", we->get("videoId")->asString(""));
				item.isPlayable = true;
			}

			const JsonValue *ci = lvm->get("contentImage") ? lvm->get("contentImage")->get("thumbnailViewModel") : nullptr;
			if(ci)
			{
				const JsonValue *srcs = ci->get("image") ? ci->get("image")->get("sources") : nullptr;
				if(srcs && srcs->size() > 0 && srcs->at(0)->get("url"))
					snprintf(item.thumbUrl, sizeof(item.thumbUrl), "%s", srcs->at(0)->get("url")->asString(""));

				const JsonValue *overlays = ci->get("overlays");
				if(overlays && overlays->size() > 0)
				{
					const JsonValue *bovm = overlays->at(0)->get("thumbnailBottomOverlayViewModel");
					const JsonValue *badges = bovm ? bovm->get("badges") : nullptr;
					if(badges && badges->size() > 0)
					{
						const JsonValue *tbvm = badges->at(0)->get("thumbnailBadgeViewModel");
						if(tbvm && tbvm->get("text"))
							snprintf(item.duration, sizeof(item.duration), "%s", tbvm->get("text")->asString(""));
					}
				}
			}

			if(item.title[0] != '\0')
				count++;
			continue;
		}

		const JsonValue *slvm = node->get("shortsLockupViewModel");
		if(slvm)
		{
			const JsonValue *om = slvm->get("overlayMetadata");
			if(om)
			{
				if(om->get("primaryText") && om->get("primaryText")->get("content"))
					snprintf(item.title, sizeof(item.title), "%s", om->get("primaryText")->get("content")->asString(""));
				if(om->get("secondaryText") && om->get("secondaryText")->get("content"))
					snprintf(item.views, sizeof(item.views), "%s", om->get("secondaryText")->get("content")->asString(""));
			}

			const JsonValue *onTap = slvm->get("onTap") ? slvm->get("onTap")->get("innertubeCommand") : nullptr;
			const JsonValue *re = onTap ? onTap->get("reelWatchEndpoint") : nullptr;
			if(re && re->get("videoId"))
			{
				snprintf(item.id, sizeof(item.id), "%s", re->get("videoId")->asString(""));
				item.isPlayable = true;
			}

			const JsonValue *thm = slvm->get("thumbnail") ? slvm->get("thumbnail")->get("sources") : nullptr;
			if(thm && thm->size() > 0 && thm->at(0)->get("url"))
				snprintf(item.thumbUrl, sizeof(item.thumbUrl), "%s", thm->at(0)->get("url")->asString(""));

			snprintf(item.duration, sizeof(item.duration), "Short");
			if(item.title[0] != '\0')
				count++;
			continue;
		}

		const JsonValue *vr = node->get("videoRenderer");
		if(vr)
		{
			if(vr->get("videoId"))
			{
				snprintf(item.id, sizeof(item.id), "%s", vr->get("videoId")->asString(""));
				item.isPlayable = true;
			}
			snprintf(item.title, sizeof(item.title), "%s", firstRunText(vr->get("title"), "(untitled)"));
			snprintf(item.duration, sizeof(item.duration), "%s", firstRunText(vr->get("lengthText"), ""));
			snprintf(item.views, sizeof(item.views), "%s", firstRunText(vr->get("viewCountText"), ""));
			snprintf(item.date, sizeof(item.date), "%s", firstRunText(vr->get("publishedTimeText"), ""));

			const JsonValue *th = vr->get("thumbnail") ? vr->get("thumbnail")->get("thumbnails") : nullptr;
			if(th && th->size() > 0 && th->at(0)->get("url"))
				snprintf(item.thumbUrl, sizeof(item.thumbUrl), "%s", th->at(0)->get("url")->asString(""));

			if(item.title[0] != '\0')
				count++;
			continue;
		}

		const JsonValue *gr = node->get("gridRenderer");
		if(gr && gr->get("items"))
		{
			const JsonValue *gri = gr->get("items");
			for(size_t gi = 0; gi < gri->size() && count < maxItems; gi++)
			{
				const JsonValue *gpr = gri->at(gi)->get("gridPlaylistRenderer");
				if(!gpr) gpr = gri->at(gi)->get("playlistRenderer");
				if(gpr)
				{
					YtChannelItem &pitem = items[count];
					pitem.id[0] = '\0';
					const char *pid = gpr->get("playlistId") ? gpr->get("playlistId")->asString("") : "";
					snprintf(pitem.id, sizeof(pitem.id), "%s", pid);
					pitem.duration[0] = '\0';
					pitem.views[0] = '\0';
					pitem.date[0] = '\0';
					pitem.thumbUrl[0] = '\0';
					pitem.isPlayable = false;

					snprintf(pitem.title, sizeof(pitem.title), "%s", firstRunText(gpr->get("title"), "(playlist)"));
					const char *vcount = firstRunText(gpr->get("videoCountShortText"), "");
					if(!vcount || vcount[0] == '\0') vcount = firstRunText(gpr->get("videoCountText"), "");
					snprintf(pitem.duration, sizeof(pitem.duration), "%s", vcount ? vcount : "Playlist");

					const JsonValue *th = gpr->get("thumbnail") ? gpr->get("thumbnail")->get("thumbnails") : nullptr;
					if(th && th->size() > 0 && th->at(0)->get("url"))
						snprintf(pitem.thumbUrl, sizeof(pitem.thumbUrl), "%s", th->at(0)->get("url")->asString(""));

					count++;
					continue;
				}

				const JsonValue *gvr = gri->at(gi)->get("gridVideoRenderer");
				if(gvr && gvr->get("videoId"))
				{
					YtChannelItem &vitem = items[count];
					vitem.id[0] = '\0';
					snprintf(vitem.id, sizeof(vitem.id), "%s", gvr->get("videoId")->asString(""));
					vitem.isPlayable = true;
					snprintf(vitem.title, sizeof(vitem.title), "%s", firstRunText(gvr->get("title"), "(untitled)"));
					snprintf(vitem.duration, sizeof(vitem.duration), "%s", firstRunText(gvr->get("lengthText"), ""));
					snprintf(vitem.views, sizeof(vitem.views), "%s", firstRunText(gvr->get("viewCountText"), ""));
					snprintf(vitem.date, sizeof(vitem.date), "%s", firstRunText(gvr->get("publishedTimeText"), ""));

					const JsonValue *th = gvr->get("thumbnail") ? gvr->get("thumbnail")->get("thumbnails") : nullptr;
					if(th && th->size() > 0 && th->at(0)->get("url"))
						snprintf(vitem.thumbUrl, sizeof(vitem.thumbUrl), "%s", th->at(0)->get("url")->asString(""));

					count++;
					continue;
				}
			}
			continue;
		}

		const JsonValue *bpt = node->get("backstagePostThreadRenderer");
		const JsonValue *bpr = bpt ? bpt->get("post") : nullptr;
		const JsonValue *post = bpr ? bpr->get("backstagePostRenderer") : nullptr;
		if(post)
		{
			snprintf(item.title, sizeof(item.title), "%s", firstRunText(post->get("contentText"), "(post)"));
			snprintf(item.date, sizeof(item.date), "%s", firstRunText(post->get("publishedTimeText"), ""));

			const JsonValue *attachment = post->get("backstageAttachment");
			const JsonValue *thumbs = nullptr;
			if(attachment)
			{
				const JsonValue *imgR = attachment->get("backstageImageRenderer");
				if(imgR)
					thumbs = imgR->get("image") ? imgR->get("image")->get("thumbnails") : nullptr;

				if(!thumbs)
				{
					const JsonValue *multiImgR = attachment->get("postMultiImageRenderer");
					const JsonValue *images = multiImgR ? multiImgR->get("images") : nullptr;
					if(images && images->size() > 0)
					{
						const JsonValue *firstImgR = images->at(0)->get("backstageImageRenderer");
						thumbs = firstImgR && firstImgR->get("image") ? firstImgR->get("image")->get("thumbnails") : nullptr;
					}
				}

				if(!thumbs)
				{
					const JsonValue *videoR = attachment->get("videoRenderer");
					thumbs = videoR && videoR->get("thumbnail") ? videoR->get("thumbnail")->get("thumbnails") : nullptr;
				}
			}
			if(thumbs && thumbs->size() > 0 && thumbs->at(thumbs->size() - 1)->get("url"))
				snprintf(item.thumbUrl, sizeof(item.thumbUrl), "%s", thumbs->at(thumbs->size() - 1)->get("url")->asString(""));

			count++;
			continue;
		}
	}

	if(outCount) *outCount = count;
	return true;
}

static const char *getPrefsFilePath()
{
	static char path[128] = "";
	if(path[0] != '\0') return path;

	FILE *fp = fopen("sd:/apps/brewtube/preferences.json", "r");
	if(fp) { fclose(fp); snprintf(path, sizeof(path), "sd:/apps/brewtube/preferences.json"); return path; }

	fp = fopen("sd:/brewtube_preferences.json", "r");
	if(fp) { fclose(fp); snprintf(path, sizeof(path), "sd:/brewtube_preferences.json"); return path; }

	fp = fopen("usb:/apps/brewtube/preferences.json", "r");
	if(fp) { fclose(fp); snprintf(path, sizeof(path), "usb:/apps/brewtube/preferences.json"); return path; }

	fp = fopen("usb:/brewtube_preferences.json", "r");
	if(fp) { fclose(fp); snprintf(path, sizeof(path), "usb:/brewtube_preferences.json"); return path; }

	FILE *test = fopen("sd:/apps/brewtube/preferences.json", "a");
	if(test) { fclose(test); snprintf(path, sizeof(path), "sd:/apps/brewtube/preferences.json"); return path; }

	test = fopen("sd:/brewtube_preferences.json", "a");
	if(test) { fclose(test); snprintf(path, sizeof(path), "sd:/brewtube_preferences.json"); return path; }

	snprintf(path, sizeof(path), "usb:/brewtube_preferences.json");
	return path;
}

static const char *getSubsFilePath()
{
	static char path[128] = "";
	if(path[0] != '\0') return path;

	FILE *fp = fopen("sd:/apps/brewtube/subscriptions.txt", "r");
	if(fp) { fclose(fp); snprintf(path, sizeof(path), "sd:/apps/brewtube/subscriptions.txt"); return path; }

	fp = fopen("sd:/brewtube_subscriptions.txt", "r");
	if(fp) { fclose(fp); snprintf(path, sizeof(path), "sd:/brewtube_subscriptions.txt"); return path; }

	fp = fopen("usb:/apps/brewtube/subscriptions.txt", "r");
	if(fp) { fclose(fp); snprintf(path, sizeof(path), "usb:/apps/brewtube/subscriptions.txt"); return path; }

	fp = fopen("usb:/brewtube_subscriptions.txt", "r");
	if(fp) { fclose(fp); snprintf(path, sizeof(path), "usb:/brewtube_subscriptions.txt"); return path; }

	FILE *test = fopen("sd:/apps/brewtube/subscriptions.txt", "a");
	if(test) { fclose(test); snprintf(path, sizeof(path), "sd:/apps/brewtube/subscriptions.txt"); return path; }

	test = fopen("sd:/brewtube_subscriptions.txt", "a");
	if(test) { fclose(test); snprintf(path, sizeof(path), "sd:/brewtube_subscriptions.txt"); return path; }

	snprintf(path, sizeof(path), "usb:/brewtube_subscriptions.txt");
	return path;
}

void prefsSave()
{
	const char *fpath = getPrefsFilePath();
	FILE *fp = fopen(fpath, "w");
	if(fp)
	{
		fprintf(fp, "{\n");
		fprintf(fp, "  \"client\": \"%s\",\n", gClientType == YT_CLIENT_VISIONOS ? "VISIONOS" : "ANDROID");
		fprintf(fp, "  \"volume\": %d,\n", gVolume);
		fprintf(fp, "  \"captions\": %s,\n", gCaptionsEnabled ? "true" : "false");
		fprintf(fp, "  \"subscriptions\": [\n");
		for(size_t i = 0; i < gSubscriptions.size(); i++)
		{
			const auto &s = gSubscriptions[i];
			fprintf(fp, "    {\"channelId\": \"%s\", \"title\": \"%s\", \"avatarUrl\": \"%s\"}%s\n",
				jsonEscape(s.channelId).c_str(),
				jsonEscape(s.title).c_str(),
				jsonEscape(s.avatarUrl).c_str(),
				(i + 1 < gSubscriptions.size()) ? "," : "");
		}
		fprintf(fp, "  ],\n");
		fprintf(fp, "  \"playlists\": [\n");
		for(size_t p = 0; p < gLocalPlaylists.size(); p++)
		{
			const auto &pl = gLocalPlaylists[p];
			fprintf(fp, "    {\n");
			fprintf(fp, "      \"id\": \"%s\",\n", jsonEscape(pl.id).c_str());
			fprintf(fp, "      \"title\": \"%s\",\n", jsonEscape(pl.title).c_str());
			fprintf(fp, "      \"items\": [\n");
			for(size_t i = 0; i < pl.items.size(); i++)
			{
				const auto &itm = pl.items[i];
				fprintf(fp, "        {\"videoId\": \"%s\", \"title\": \"%s\", \"author\": \"%s\", \"duration\": \"%s\", \"thumbUrl\": \"%s\"}%s\n",
					jsonEscape(itm.videoId).c_str(),
					jsonEscape(itm.title).c_str(),
					jsonEscape(itm.author).c_str(),
					jsonEscape(itm.duration).c_str(),
					jsonEscape(itm.thumbUrl).c_str(),
					(i + 1 < pl.items.size()) ? "," : "");
			}
			fprintf(fp, "      ]\n");
			fprintf(fp, "    }%s\n", (p + 1 < gLocalPlaylists.size()) ? "," : "");
		}
		fprintf(fp, "  ]\n");
		fprintf(fp, "}\n");
		fclose(fp);
	}

	const char *subsPath = getSubsFilePath();
	FILE *sfp = fopen(subsPath, "w");
	if(sfp)
	{
		for(const auto &s : gSubscriptions)
			fprintf(sfp, "%s|%s|%s\n", s.channelId, s.title, s.avatarUrl);
		fclose(sfp);
	}
}

void prefsLoad()
{
	getPrefsLock().lock();
	if(gPrefsLoaded)
	{
		getPrefsLock().unlock();
		return;
	}
	gPrefsLoaded = true;

	const char *fpath = getPrefsFilePath();
	FILE *fp = fopen(fpath, "rb");
	if(fp)
	{
		fseek(fp, 0, SEEK_END);
		long sz = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if(sz > 0 && sz < 1024 * 1024)
		{
			std::string content;
			content.resize(sz);
			fread(&content[0], 1, sz, fp);
			fclose(fp);
			fp = nullptr;

			JsonValue root;
			if(jsonParse(content.c_str(), content.size(), root))
			{
				const JsonValue *c = root.get("client");
				if(c)
				{
					if(c->type == JsonType::String && strcmp(c->asString(""), "VISIONOS") == 0)
						gClientType = YT_CLIENT_VISIONOS;
					else if(c->type == JsonType::Number && c->asInt(0) == 1)
						gClientType = YT_CLIENT_VISIONOS;
					else
						gClientType = YT_CLIENT_ANDROID;
				}

				const JsonValue *vol = root.get("volume");
				if(vol)
				{
					int v = vol->asInt(200);
					if(v < 0) v = 0;
					if(v > 255) v = 255;
					gVolume = v;
				}

				const JsonValue *caps = root.get("captions");
				if(caps)
				{
					gCaptionsEnabled = caps->asBool(false);
				}

				const JsonValue *subs = root.get("subscriptions");
				if(subs && subs->type == JsonType::Array)
				{
					gSubscriptions.clear();
					for(const auto &item : subs->arr)
					{
						const JsonValue *cid = item.get("channelId");
						if(cid && cid->asString("")[0] != '\0')
						{
							YtSubscription s;
							memset(&s, 0, sizeof(s));
							snprintf(s.channelId, sizeof(s.channelId), "%s", cid->asString(""));
							const JsonValue *title = item.get("title");
							snprintf(s.title, sizeof(s.title), "%s", title ? title->asString("") : s.channelId);
							const JsonValue *av = item.get("avatarUrl");
							snprintf(s.avatarUrl, sizeof(s.avatarUrl), "%s", av ? av->asString("") : "");
							gSubscriptions.push_back(s);
						}
					}
				}

				const JsonValue *pls = root.get("playlists");
				if(pls && pls->type == JsonType::Array)
				{
					gLocalPlaylists.clear();
					for(const auto &pNode : pls->arr)
					{
						const JsonValue *pid = pNode.get("id");
						const JsonValue *ptitle = pNode.get("title");
						if(pid && pid->asString("")[0] != '\0')
						{
							YtLocalPlaylist pl;
							pl.id[0] = '\0';
							pl.title[0] = '\0';
							snprintf(pl.id, sizeof(pl.id), "%s", pid->asString(""));
							snprintf(pl.title, sizeof(pl.title), "%s", ptitle ? ptitle->asString(pl.id) : pl.id);

							const JsonValue *items = pNode.get("items");
							if(items && items->type == JsonType::Array)
							{
								for(const auto &iNode : items->arr)
								{
									const JsonValue *vid = iNode.get("videoId");
									if(vid && vid->asString("")[0] != '\0')
									{
										YtLocalPlaylistItem itm;
										memset(&itm, 0, sizeof(itm));
										snprintf(itm.videoId, sizeof(itm.videoId), "%s", vid->asString(""));
										const JsonValue *t = iNode.get("title");
										snprintf(itm.title, sizeof(itm.title), "%s", t ? t->asString(itm.videoId) : itm.videoId);
										const JsonValue *a = iNode.get("author");
										snprintf(itm.author, sizeof(itm.author), "%s", a ? a->asString("") : "");
										const JsonValue *d = iNode.get("duration");
										snprintf(itm.duration, sizeof(itm.duration), "%s", d ? d->asString("") : "");
										const JsonValue *th = iNode.get("thumbUrl");
										snprintf(itm.thumbUrl, sizeof(itm.thumbUrl), "%s", th ? th->asString("") : "");
										pl.items.push_back(itm);
									}
								}
							}
							gLocalPlaylists.push_back(pl);
						}
					}
				}
			}
		}
		if(fp) fclose(fp);
	}

	if(gSubscriptions.empty())
	{
		const char *subsPath = getSubsFilePath();
		FILE *sfp = fopen(subsPath, "r");
		if(sfp)
		{
			char line[512];
			while(fgets(line, sizeof(line), sfp))
			{
				int len = strlen(line);
				while(len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) line[--len] = '\0';
				if(len == 0) continue;

				char *p1 = strchr(line, '|');
				if(!p1) continue;
				*p1 = '\0';
				char *p2 = strchr(p1 + 1, '|');
				if(p2) *p2 = '\0';

				YtSubscription sub;
				memset(&sub, 0, sizeof(sub));
				snprintf(sub.channelId, sizeof(sub.channelId), "%s", line);
				snprintf(sub.title, sizeof(sub.title), "%s", p1 + 1);
				if(p2) snprintf(sub.avatarUrl, sizeof(sub.avatarUrl), "%s", p2 + 1);
				gSubscriptions.push_back(sub);
			}
			fclose(sfp);
			if(!gSubscriptions.empty())
				prefsSave();
		}
	}
	getPrefsLock().unlock();
}

std::vector<YtSubscription> ytGetSubscriptions()
{
	prefsLoad();
	getPrefsLock().lock();
	std::vector<YtSubscription> list = gSubscriptions;
	getPrefsLock().unlock();
	return list;
}

bool ytIsSubscribed(const char *channelId)
{
	if(!channelId || channelId[0] == '\0') return false;
	prefsLoad();
	getPrefsLock().lock();
	for(const auto &s : gSubscriptions)
	{
		if(strcmp(s.channelId, channelId) == 0)
		{
			getPrefsLock().unlock();
			return true;
		}
	}
	getPrefsLock().unlock();
	return false;
}

void ytToggleSubscription(const char *channelId, const char *title, const char *avatarUrl)
{
	if(!channelId || channelId[0] == '\0') return;
	prefsLoad();
	getPrefsLock().lock();

	bool found = false;
	for(size_t i = 0; i < gSubscriptions.size(); i++)
	{
		if(strcmp(gSubscriptions[i].channelId, channelId) == 0)
		{
			gSubscriptions.erase(gSubscriptions.begin() + i);
			found = true;
			break;
		}
	}

	if(!found)
	{
		YtSubscription s;
		memset(&s, 0, sizeof(s));
		snprintf(s.channelId, sizeof(s.channelId), "%s", channelId);
		snprintf(s.title, sizeof(s.title), "%s", title && title[0] ? title : channelId);
		snprintf(s.avatarUrl, sizeof(s.avatarUrl), "%s", avatarUrl ? avatarUrl : "");
		gSubscriptions.push_back(s);
	}

	prefsSave();
	getPrefsLock().unlock();
}

std::vector<YtLocalPlaylist> ytGetLocalPlaylists()
{
	prefsLoad();
	getPrefsLock().lock();
	std::vector<YtLocalPlaylist> pls = gLocalPlaylists;
	getPrefsLock().unlock();
	return pls;
}

bool ytCreateLocalPlaylist(const char *title, char *outId, int outIdSize)
{
	if(!title || title[0] == '\0') return false;
	prefsLoad();
	getPrefsLock().lock();

	char idBuf[64];
	snprintf(idBuf, sizeof(idBuf), "pl_%08x", (unsigned int)time(nullptr) + (unsigned int)gLocalPlaylists.size());
	if(outId && outIdSize > 0)
		snprintf(outId, outIdSize, "%s", idBuf);

	YtLocalPlaylist pl;
	pl.id[0] = '\0';
	pl.title[0] = '\0';
	snprintf(pl.id, sizeof(pl.id), "%s", idBuf);
	snprintf(pl.title, sizeof(pl.title), "%s", title);
	gLocalPlaylists.push_back(pl);

	prefsSave();
	getPrefsLock().unlock();
	return true;
}

bool ytDeleteLocalPlaylist(const char *playlistId)
{
	if(!playlistId || playlistId[0] == '\0') return false;
	prefsLoad();
	getPrefsLock().lock();

	bool found = false;
	for(size_t i = 0; i < gLocalPlaylists.size(); i++)
	{
		if(strcmp(gLocalPlaylists[i].id, playlistId) == 0)
		{
			gLocalPlaylists.erase(gLocalPlaylists.begin() + i);
			found = true;
			break;
		}
	}

	if(found) prefsSave();
	getPrefsLock().unlock();
	return found;
}

bool ytAddToLocalPlaylist(const char *playlistId, const YtLocalPlaylistItem &item)
{
	if(!playlistId || playlistId[0] == '\0' || item.videoId[0] == '\0') return false;
	prefsLoad();
	getPrefsLock().lock();

	bool found = false;
	for(auto &pl : gLocalPlaylists)
	{
		if(strcmp(pl.id, playlistId) == 0)
		{
			for(const auto &it : pl.items)
			{
				if(strcmp(it.videoId, item.videoId) == 0)
				{
					getPrefsLock().unlock();
					return true;
				}
			}
			pl.items.push_back(item);
			found = true;
			break;
		}
	}

	if(found) prefsSave();
	getPrefsLock().unlock();
	return found;
}

bool ytRemoveFromLocalPlaylist(const char *playlistId, const char *videoId)
{
	if(!playlistId || playlistId[0] == '\0' || !videoId || videoId[0] == '\0') return false;
	prefsLoad();
	getPrefsLock().lock();

	bool found = false;
	for(auto &pl : gLocalPlaylists)
	{
		if(strcmp(pl.id, playlistId) == 0)
		{
			for(size_t i = 0; i < pl.items.size(); i++)
			{
				if(strcmp(pl.items[i].videoId, videoId) == 0)
				{
					pl.items.erase(pl.items.begin() + i);
					found = true;
					break;
				}
			}
			break;
		}
	}

	if(found) prefsSave();
	getPrefsLock().unlock();
	return found;
}

bool ytGetLocalPlaylist(const char *playlistId, YtLocalPlaylist &outPlaylist)
{
	if(!playlistId || playlistId[0] == '\0') return false;
	prefsLoad();
	getPrefsLock().lock();

	bool found = false;
	for(const auto &pl : gLocalPlaylists)
	{
		if(strcmp(pl.id, playlistId) == 0)
		{
			outPlaylist = pl;
			found = true;
			break;
		}
	}

	getPrefsLock().unlock();
	return found;
}

bool ytFetchSearchSuggestions(const char *query, std::vector<std::string> &suggestions, int maxSuggestions)
{
	suggestions.clear();
	if(!query || query[0] == '\0') return false;

	std::string encoded;
	for(const char *p = query; *p; p++)
	{
		unsigned char c = (unsigned char)*p;
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			encoded += c;
		else if(c == ' ')
			encoded += '+';
		else
		{
			char buf[4];
			snprintf(buf, sizeof(buf), "%%%02X", c);
			encoded += buf;
		}
	}

	std::string url = "https://suggestqueries-clients6.youtube.com/complete/search?ds=yt&hl=en&gl=us&client=youtube&gs_ri=youtube&q=" + encoded;
	std::string response;
	if(!httpGetBinary(url.c_str(), response, 4) || response.empty())
		return false;

	const char *start = strchr(response.c_str(), '[');
	const char *end = strrchr(response.c_str(), ']');
	if(!start || !end || end <= start)
		return false;

	std::string jsonStr(start, end - start + 1);
	JsonValue root;
	if(!jsonParse(jsonStr.c_str(), jsonStr.size(), root) || root.type != JsonType::Array || root.arr.size() < 2)
		return false;

	const JsonValue *items = root.at(1);
	if(!items || items->type != JsonType::Array)
		return false;

	for(const auto &it : items->arr)
	{
		if(it.type == JsonType::Array && it.arr.size() > 0)
		{
			const JsonValue *s = it.at(0);
			if(s && s->type == JsonType::String && s->asString("")[0] != '\0')
			{
				suggestions.push_back(s->asString(""));
				if((int)suggestions.size() >= maxSuggestions)
					break;
			}
		}
	}

	return !suggestions.empty();
}

int ytFetchSubscriptionsFeed(YtResult *results, int maxResults, char *err, int errSize)
{
	std::vector<YtSubscription> subs = ytGetSubscriptions();
	if(subs.empty())
	{
		snprintf(err, errSize, "No channels subscribed yet");
		return 0;
	}

	int count = 0;
	std::unique_ptr<YtChannelItem[]> cItems(new YtChannelItem[10]);

	for(const auto &sub : subs)
	{
		if(count >= maxResults) break;
		int cCount = 0;
		char cErr[128];
		if(ytChannelBrowse(sub.channelId, nullptr, YT_CHAN_TAB_VIDEOS, YT_CHAN_FILTER_NEWEST, cItems.get(), 6, &cCount, cErr, sizeof(cErr)))
		{
			for(int i = 0; i < cCount && count < maxResults; i++)
			{
				if(!cItems[i].isPlayable || cItems[i].id[0] == '\0') continue;
				YtResult &r = results[count];
				memset(&r, 0, sizeof(r));
				r.isChannel = false;
				r.isPlaylist = false;
				snprintf(r.videoId, sizeof(r.videoId), "%s", cItems[i].id);
				snprintf(r.title, sizeof(r.title), "%s", cItems[i].title);
				snprintf(r.channelId, sizeof(r.channelId), "%s", sub.channelId);
				snprintf(r.author, sizeof(r.author), "%s", sub.title[0] ? sub.title : sub.channelId);
				snprintf(r.avatarUrl, sizeof(r.avatarUrl), "%s", sub.avatarUrl);
				snprintf(r.lengthText, sizeof(r.lengthText), "%s", cItems[i].duration);
				snprintf(r.viewCountText, sizeof(r.viewCountText), "%s", cItems[i].views);
				snprintf(r.publishedText, sizeof(r.publishedText), "%s", cItems[i].date);
				count++;
			}
		}
	}

	if(count == 0)
		snprintf(err, errSize, "No videos found in subscriptions");
	return count;
}

static void extractVideosFromNode(const JsonValue &node, YtResult *results, int &count, int maxResults)
{
	if(count >= maxResults) return;

	if(node.type == JsonType::Object)
	{
		const JsonValue *vr = node.get("videoRenderer");
		if(!vr) vr = node.get("compactVideoRenderer");
		if(!vr) vr = node.get("gridVideoRenderer");
		if(vr && vr->get("videoId"))
		{
			const char *vid = vr->get("videoId")->asString("");
			if(vid[0] != '\0')
			{
				YtResult &r = results[count];
				memset(&r, 0, sizeof(r));
				r.isChannel = false;
				r.isPlaylist = false;
				snprintf(r.videoId, sizeof(r.videoId), "%s", vid);
				snprintf(r.channelId, sizeof(r.channelId), "%s", extractChannelId(vr));
				extractAvatarUrl(vr, r.avatarUrl, sizeof(r.avatarUrl));
				snprintf(r.title, sizeof(r.title), "%s", firstRunText(vr->get("title"), "(untitled)"));

				const char *author = firstRunText(vr->get("shortBylineText"), "");
				if(!author || author[0] == '\0') author = firstRunText(vr->get("longBylineText"), "");
				if(!author || author[0] == '\0') author = firstRunText(vr->get("ownerText"), "");
				snprintf(r.author, sizeof(r.author), "%s", author ? author : "");

				snprintf(r.lengthText, sizeof(r.lengthText), "%s", firstRunText(vr->get("lengthText"), ""));
				if(r.lengthText[0] == '\0')
				{
					const JsonValue *overlays = vr->get("thumbnailOverlays");
					if(overlays && overlays->type == JsonType::Array)
					{
						for(const auto &ov : overlays->arr)
						{
							const JsonValue *tov = ov.get("thumbnailOverlayTimeStatusRenderer");
							if(tov)
							{
								const char *dur = firstRunText(tov->get("text"), "");
								if(dur && dur[0] != '\0')
								{
									snprintf(r.lengthText, sizeof(r.lengthText), "%s", dur);
									break;
								}
							}
						}
					}
				}

				const char *views = firstRunText(vr->get("shortViewCountText"), "");
				if(!views || views[0] == '\0') views = firstRunText(vr->get("viewCountText"), "");
				snprintf(r.viewCountText, sizeof(r.viewCountText), "%s", views ? views : "");

				if(r.viewCountText[0] == '\0')
				{
					const JsonValue *badges = vr->get("badges");
					if(badges && badges->type == JsonType::Array)
					{
						for(const auto &b : badges->arr)
						{
							const JsonValue *mbr = b.get("metadataBadgeRenderer");
							if(mbr && mbr->get("label"))
							{
								const char *lbl = mbr->get("label")->asString("");
								if(lbl && lbl[0] != '\0')
								{
									snprintf(r.viewCountText, sizeof(r.viewCountText), "%s", lbl);
									break;
								}
							}
						}
					}
				}

				snprintf(r.publishedText, sizeof(r.publishedText), "%s", firstRunText(vr->get("publishedTimeText"), ""));
				count++;
				if(count >= maxResults) return;
			}
		}

		const JsonValue *lvm = node.get("lockupViewModel");
		if(lvm)
		{
			const char *cid = lvm->get("contentId") ? lvm->get("contentId")->asString("") : "";
			const char *ctype = lvm->get("contentType") ? lvm->get("contentType")->asString("") : "";
			if(cid[0] != '\0')
			{
				const JsonValue *meta = lvm->get("metadata");
				const JsonValue *lmvm = meta ? meta->get("lockupMetadataViewModel") : nullptr;
				const JsonValue *titleVal = lmvm && lmvm->get("title") ? lmvm->get("title")->get("content") : nullptr;
				const char *title = titleVal ? titleVal->asString("") : "";
				if(title[0] != '\0')
				{
					YtResult &r = results[count];
					memset(&r, 0, sizeof(r));

					bool isPl = (strcmp(ctype, "LOCKUP_CONTENT_TYPE_ALBUM") == 0 ||
					             strcmp(ctype, "LOCKUP_CONTENT_TYPE_PLAYLIST") == 0 ||
					             strncmp(cid, "RD", 2) == 0 ||
					             strncmp(cid, "PL", 2) == 0 ||
					             strlen(cid) > 11);

					r.isChannel = false;
					r.isPlaylist = isPl;
					if(isPl)
					{
						snprintf(r.playlistId, sizeof(r.playlistId), "%s", cid);
						snprintf(r.videoId, sizeof(r.videoId), "%s", cid);
						r.lengthText[0] = '\0';
					}
					else
					{
						snprintf(r.videoId, sizeof(r.videoId), "%s", cid);
					}
					snprintf(r.title, sizeof(r.title), "%s", title);

					const JsonValue *cmvm = lmvm ? (lmvm->get("metadata") ? lmvm->get("metadata")->get("contentMetadataViewModel") : nullptr) : nullptr;
					const JsonValue *rows = cmvm ? cmvm->get("metadataRows") : (lmvm ? lmvm->get("metadataRows") : nullptr);
					if(rows && rows->size() > 0)
					{
						const JsonValue *parts = rows->at(0)->get("metadataParts");
						if(parts && parts->size() > 0 && parts->at(0)->get("text") && parts->at(0)->get("text")->get("content"))
							snprintf(r.author, sizeof(r.author), "%s", parts->at(0)->get("text")->get("content")->asString(""));
						if(rows->size() > 1)
						{
							const JsonValue *parts2 = rows->at(1)->get("metadataParts");
							if(parts2 && parts2->size() > 0 && parts2->at(0)->get("text") && parts2->at(0)->get("text")->get("content"))
								snprintf(r.viewCountText, sizeof(r.viewCountText), "%s", parts2->at(0)->get("text")->get("content")->asString(""));
							if(parts2 && parts2->size() > 1 && parts2->at(1)->get("text") && parts2->at(1)->get("text")->get("content"))
								snprintf(r.publishedText, sizeof(r.publishedText), "%s", parts2->at(1)->get("text")->get("content")->asString(""));
						}
					}

					const JsonValue *cimg = lvm->get("contentImage");
					const JsonValue *tvm = cimg ? cimg->get("thumbnailViewModel") : nullptr;
					if(!tvm && cimg && cimg->get("collectionThumbnailViewModel"))
						tvm = cimg->get("collectionThumbnailViewModel")->get("primaryThumbnail") ? cimg->get("collectionThumbnailViewModel")->get("primaryThumbnail")->get("thumbnailViewModel") : nullptr;
					if(tvm && tvm->get("image") && tvm->get("image")->get("sources"))
					{
						const JsonValue *srcs = tvm->get("image")->get("sources");
						if(srcs->size() > 0 && srcs->at(0)->get("url"))
							snprintf(r.avatarUrl, sizeof(r.avatarUrl), "%s", srcs->at(0)->get("url")->asString(""));
					}
					if(tvm && tvm->get("overlays") && tvm->get("overlays")->type == JsonType::Array)
					{
						for(const auto &ov : tvm->get("overlays")->arr)
						{
							const JsonValue *tobvm = ov.get("thumbnailOverlayBadgeViewModel");
							const JsonValue *tbadges = tobvm ? tobvm->get("thumbnailBadges") : nullptr;
							if(tbadges && tbadges->size() > 0)
							{
								const JsonValue *tbvm = tbadges->at(0)->get("thumbnailBadgeViewModel");
								if(tbvm && tbvm->get("text"))
								{
									snprintf(r.lengthText, sizeof(r.lengthText), "%s", tbvm->get("text")->asString(""));
									break;
								}
							}
						}
					}

					count++;
					if(count >= maxResults) return;
				}
			}
		}

		for(const auto &kv : node.obj)
			extractVideosFromNode(kv.second, results, count, maxResults);
	}
	else if(node.type == JsonType::Array)
	{
		for(const auto &v : node.arr)
			extractVideosFromNode(v, results, count, maxResults);
	}
}

int ytBrowseCategory(const char *browseId, YtResult *results, int maxResults, char *err, int errSize)
{
	if(!browseId || browseId[0] == '\0')
	{
		snprintf(err, errSize, "Invalid category ID");
		return 0;
	}

	std::string body = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260925.01.00\",\"hl\":\"en\",\"gl\":\"US\"}},\"browseId\":\"";
	body += jsonEscape(browseId);
	body += "\"}";

	std::string response;
	if(!httpPost("https://www.youtube.com/youtubei/v1/browse", body.c_str(), response))
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

	int count = 0;
	extractVideosFromNode(root, results, count, maxResults);

	if(count == 0)
		snprintf(err, errSize, "No videos found in this category");

	return count;
}

static void extractPlaylistItems(const JsonValue &node, YtPlaylistItem *items, int &count, int maxItems)
{
	if(count >= maxItems) return;

	if(node.type == JsonType::Object)
	{
		const JsonValue *pvr = node.get("playlistVideoRenderer");
		if(pvr && pvr->get("videoId"))
		{
			const char *vid = pvr->get("videoId")->asString("");
			if(vid[0] != '\0')
			{
				YtPlaylistItem &itm = items[count];
				memset(&itm, 0, sizeof(itm));
				snprintf(itm.videoId, sizeof(itm.videoId), "%s", vid);
				snprintf(itm.title, sizeof(itm.title), "%s", firstRunText(pvr->get("title"), "(untitled)"));
				const char *author = firstRunText(pvr->get("shortBylineText"), "");
				snprintf(itm.author, sizeof(itm.author), "%s", author ? author : "");
				snprintf(itm.duration, sizeof(itm.duration), "%s", firstRunText(pvr->get("lengthText"), ""));
				const JsonValue *th = pvr->get("thumbnail") ? pvr->get("thumbnail")->get("thumbnails") : nullptr;
				if(th && th->size() > 0 && th->at(0)->get("url"))
					snprintf(itm.thumbUrl, sizeof(itm.thumbUrl), "%s", th->at(0)->get("url")->asString(""));
				count++;
				if(count >= maxItems) return;
			}
		}

		const JsonValue *lvm = node.get("lockupViewModel");
		if(lvm)
		{
			const char *cid = lvm->get("contentId") ? lvm->get("contentId")->asString("") : "";
			if(cid[0] != '\0' && strlen(cid) == 11)
			{
				const JsonValue *meta = lvm->get("metadata");
				const JsonValue *lmvm = meta ? meta->get("lockupMetadataViewModel") : nullptr;
				const JsonValue *tVal = lmvm && lmvm->get("title") ? lmvm->get("title")->get("content") : nullptr;
				const char *title = tVal ? tVal->asString("") : "";
				if(title[0] != '\0')
				{
					YtPlaylistItem &itm = items[count];
					memset(&itm, 0, sizeof(itm));
					snprintf(itm.videoId, sizeof(itm.videoId), "%s", cid);
					snprintf(itm.title, sizeof(itm.title), "%s", title);

					const JsonValue *cmvm = lmvm ? (lmvm->get("metadata") ? lmvm->get("metadata")->get("contentMetadataViewModel") : nullptr) : nullptr;
					const JsonValue *rows = cmvm ? cmvm->get("metadataRows") : (lmvm ? lmvm->get("metadataRows") : nullptr);
					if(rows && rows->size() > 0)
					{
						const JsonValue *parts = rows->at(0)->get("metadataParts");
						if(parts && parts->size() > 0 && parts->at(0)->get("text") && parts->at(0)->get("text")->get("content"))
							snprintf(itm.author, sizeof(itm.author), "%s", parts->at(0)->get("text")->get("content")->asString(""));
					}

					const JsonValue *ci = lvm->get("contentImage") ? lvm->get("contentImage")->get("thumbnailViewModel") : nullptr;
					if(ci)
					{
						const JsonValue *srcs = ci->get("image") ? ci->get("image")->get("sources") : nullptr;
						if(srcs && srcs->size() > 0 && srcs->at(0)->get("url"))
							snprintf(itm.thumbUrl, sizeof(itm.thumbUrl), "%s", srcs->at(0)->get("url")->asString(""));

						const JsonValue *overlays = ci->get("overlays");
						if(overlays && overlays->size() > 0)
						{
							const JsonValue *bovm = overlays->at(0)->get("thumbnailBottomOverlayViewModel");
							const JsonValue *badges = bovm ? bovm->get("badges") : nullptr;
							if(badges && badges->size() > 0)
							{
								const JsonValue *tbvm = badges->at(0)->get("thumbnailBadgeViewModel");
								if(tbvm && tbvm->get("text"))
									snprintf(itm.duration, sizeof(itm.duration), "%s", tbvm->get("text")->asString(""));
							}
						}
					}
					count++;
					if(count >= maxItems) return;
				}
			}
		}

		for(const auto &kv : node.obj)
			extractPlaylistItems(kv.second, items, count, maxItems);
	}
	else if(node.type == JsonType::Array)
	{
		for(const auto &v : node.arr)
			extractPlaylistItems(v, items, count, maxItems);
	}
}

bool ytPlaylistBrowse(const char *playlistId, char *titleOut, int titleOutSize, char *authorOut, int authorOutSize, YtPlaylistItem *items, int maxItems, int *outCount, char *err, int errSize)
{
	if(!playlistId || playlistId[0] == '\0')
	{
		snprintf(err, errSize, "Invalid playlist ID");
		return false;
	}

	std::string browseId = playlistId;
	if(strncmp(playlistId, "VL", 2) != 0)
		browseId = std::string("VL") + playlistId;

	std::string body = "{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260925.01.00\",\"hl\":\"en\",\"gl\":\"US\"}},\"browseId\":\"";
	body += jsonEscape(browseId.c_str());
	body += "\"}";

	std::string response;
	if(!httpPost("https://www.youtube.com/youtubei/v1/browse", body.c_str(), response))
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

	if(titleOut && titleOutSize > 0) titleOut[0] = '\0';
	if(authorOut && authorOutSize > 0) authorOut[0] = '\0';

	const JsonValue *hdr = root.get("header");
	const JsonValue *plh = hdr ? hdr->get("playlistHeaderRenderer") : nullptr;
	if(plh)
	{
		if(titleOut && titleOutSize > 0)
			snprintf(titleOut, titleOutSize, "%s", firstRunText(plh->get("title"), "(playlist)"));
		if(authorOut && authorOutSize > 0)
			snprintf(authorOut, authorOutSize, "%s", firstRunText(plh->get("ownerText"), ""));
	}

	int count = 0;
	extractPlaylistItems(root, items, count, maxItems);
	if(outCount) *outCount = count;
	return count > 0;
}
