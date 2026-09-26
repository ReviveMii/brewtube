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

#include <curl/curl.h>
#include <wiisocket.h>
#include <gctypes.h>

#include "drivers/Mutex.h"
#include "drivers/Cond.h"
#include "drivers/Thread.h"
#include "netstream.h"
#include "cacert_pem.h"

const int64_t CACHE_SIZE = 3 * 1024 * 1024;

struct NetStream
{
	CURL *curl = nullptr;
	Thread thread;
	Mutex lock;
	Cond spaceCond;
	Cond dataCond;
	Cond headerCond;

	char url[4096];
	uint8_t *cache = nullptr;
	int64_t cacheSize = 0;
	int64_t wrPos = 0;
	int64_t rdPos = 0;
	int64_t total = -1;
	bool headersDone = false;
	bool eof = false;
	bool failed = false;
	bool quit = false;
	bool restart = false;
	int64_t restartOffset = 0;
	long httpStatus = 0;
	char errorText[128] = "";
};

namespace {

struct curl_blob caInfo = { (u8 *)cacert_pem, cacert_pem_size, CURL_BLOB_COPY };

int progressCb(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	NetStream *s = static_cast<NetStream *>(clientp);
	return (s && s->quit) ? 1 : 0;
}

size_t headerCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	NetStream *s = static_cast<NetStream *>(userdata);
	size_t len = size * nmemb;

	s->lock.lock();
	if(len > 15 && strncasecmp(ptr, "Content-Length:", 15) == 0 && s->total < 0)
		s->total = atoll(ptr + 15);
	else if(len > 14 && strncasecmp(ptr, "Content-Range:", 14) == 0)
	{
		const char *slash = strchr(ptr, '/');
		if(slash)
			s->total = atoll(slash + 1);
	}
	s->lock.unlock();

	if(len <= 2)
	{
		long code = 0;
		curl_easy_getinfo(s->curl, CURLINFO_RESPONSE_CODE, &code);

		if(code == 0 || code == 100 || (code >= 300 && code < 400))
		{
			s->lock.lock();
			s->total = -1;
			s->lock.unlock();
			return len;
		}

		s->lock.lock();
		s->httpStatus = code;
		if(code >= 400)
		{
			s->failed = true;
			snprintf(s->errorText, sizeof(s->errorText), "HTTP error %ld", code);
		}
		s->headersDone = true;
		s->headerCond.signal();
		s->lock.unlock();
	}

	return len;
}

size_t writeCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	NetStream *s = static_cast<NetStream *>(userdata);
	size_t total = size * nmemb;
	size_t done = 0;

	s->lock.lock();
	if(!s->headersDone)
	{
		long code = 0;
		curl_easy_getinfo(s->curl, CURLINFO_RESPONSE_CODE, &code);
		if(code == 200 || code == 206)
		{
			s->httpStatus = code;
			s->headersDone = true;
			s->headerCond.signal();
		}
	}
	int64_t cSize = s->cacheSize > 0 ? s->cacheSize : CACHE_SIZE;
	while(done < total && !s->quit && !s->restart)
	{
		while(!s->quit && !s->restart && s->wrPos - s->rdPos >= cSize)
			s->spaceCond.wait(s->lock);
		if(s->quit || s->restart)
			break;

		int64_t pos = s->wrPos % cSize;
		size_t chunk = total - done;
		if(chunk > (size_t)(cSize - pos))
			chunk = cSize - pos;
		if(chunk > (size_t)(cSize - (s->wrPos - s->rdPos)))
			chunk = cSize - (s->wrPos - s->rdPos);

		memcpy(s->cache + pos, ptr + done, chunk);
		s->wrPos += chunk;
		done += chunk;
		s->dataCond.signal();
	}
	bool abort = s->quit || s->restart;
	s->lock.unlock();

	return abort ? 0 : done;
}

void *threadEntry(void *arg)
{
	NetStream *s = static_cast<NetStream *>(arg);

	for(;;)
	{
		s->lock.lock();
		int64_t from = s->restartOffset;
		s->restart = false;
		s->headersDone = false;
		s->httpStatus = 0;
		s->lock.unlock();

		char range[32];
		if(from > 0)
		{
			snprintf(range, sizeof(range), "%lld-", (long long)from);
			curl_easy_setopt(s->curl, CURLOPT_RANGE, range);
		}
		else
			curl_easy_setopt(s->curl, CURLOPT_RANGE, nullptr);

		CURLcode res = curl_easy_perform(s->curl);

		s->lock.lock();
		if(s->quit)
		{
			s->lock.unlock();
			break;
		}

		if(s->restart)
		{
			s->wrPos = s->restartOffset;
			s->rdPos = s->restartOffset;
			s->spaceCond.signal();
			s->dataCond.signal();
			s->lock.unlock();
			continue;
		}

		long finalCode = 0;
		curl_easy_getinfo(s->curl, CURLINFO_RESPONSE_CODE, &finalCode);
		if(finalCode > 0)
			s->httpStatus = finalCode;

		if(res != CURLE_OK || (s->httpStatus != 200 && s->httpStatus != 206))
		{
			s->failed = true;
			if(s->errorText[0] == '\0')
			{
				snprintf(s->errorText, sizeof(s->errorText), "%s",
					res != CURLE_OK ? curl_easy_strerror(res) : "Server rejected request");
			}
		}
		s->eof = true;
		s->headersDone = true;
		s->headerCond.signal();
		s->dataCond.signal();
		s->lock.unlock();
		break;
	}

	return nullptr;
}

}

extern "C" int netInit()
{
	static Mutex netLock;
	netLock.lock();

	static int inited = 0;
	if(inited > 0)
	{
		netLock.unlock();
		return 1;
	}

	for(int attempt = 0; attempt < 10; attempt++)
	{
		if(wiisocket_init() == 0)
		{
			curl_global_init(CURL_GLOBAL_ALL);
			inited = 1;
			netLock.unlock();
			return 1;
		}
		usleep(500000);
	}

	netLock.unlock();
	return 0;
}

extern "C" int netIsUrl(const char *path)
{
	return strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0;
}

extern "C" NetStream *netOpenEx(const char *url, char *err, int errSize, int64_t cacheSize)
{
	if(!netInit())
	{
		snprintf(err, errSize, "Could not bring up the network connection");
		return nullptr;
	}

	NetStream *s = new NetStream();
	snprintf(s->url, sizeof(s->url), "%s", url);

	s->cacheSize = cacheSize > 0 ? cacheSize : CACHE_SIZE;
	s->cache = (uint8_t *)malloc(s->cacheSize);
	s->curl = curl_easy_init();
	if(!s->cache || !s->curl)
	{
		snprintf(err, errSize, "Out of memory");
		netClose(s);
		return nullptr;
	}

	curl_easy_setopt(s->curl, CURLOPT_URL, s->url);
	curl_easy_setopt(s->curl, CURLOPT_WRITEFUNCTION, writeCb);
	curl_easy_setopt(s->curl, CURLOPT_WRITEDATA, s);
	curl_easy_setopt(s->curl, CURLOPT_HEADERFUNCTION, headerCb);
	curl_easy_setopt(s->curl, CURLOPT_HEADERDATA, s);
	curl_easy_setopt(s->curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(s->curl, CURLOPT_CAINFO_BLOB, &caInfo);
	curl_easy_setopt(s->curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(s->curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(s->curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
	curl_easy_setopt(s->curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(s->curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(s->curl, CURLOPT_PROTOCOLS_STR, "HTTP,HTTPS");
	curl_easy_setopt(s->curl, CURLOPT_XFERINFOFUNCTION, progressCb);
	curl_easy_setopt(s->curl, CURLOPT_XFERINFODATA, s);
	curl_easy_setopt(s->curl, CURLOPT_NOPROGRESS, 0L);

	if(!s->thread.start(threadEntry, s, 32 * 1024, ThreadPriority::Normal))
	{
		snprintf(err, errSize, "Could not start network thread");
		netClose(s);
		return nullptr;
	}

	s->lock.lock();
	while(!s->headersDone && !s->quit)
		s->headerCond.wait(s->lock);
	bool ok = !s->quit && !s->failed && (s->httpStatus == 200 || s->httpStatus == 206);
	char errCopy[128];
	snprintf(errCopy, sizeof(errCopy), "%s", s->errorText);
	s->lock.unlock();

	if(!ok)
	{
		snprintf(err, errSize, "%s", errCopy[0] ? errCopy : "Could not open stream");
		netClose(s);
		return nullptr;
	}

	return s;
}

extern "C" NetStream *netOpen(const char *url, char *err, int errSize)
{
	return netOpenEx(url, err, errSize, CACHE_SIZE);
}

extern "C" void netAbort(NetStream *s)
{
	if(!s)
		return;

	s->lock.lock();
	s->quit = true;
	s->spaceCond.signal();
	s->dataCond.signal();
	s->headerCond.signal();
	s->lock.unlock();
}

extern "C" void netClose(NetStream *s)
{
	if(!s)
		return;

	netAbort(s);

	if(s->thread.isRunning())
		s->thread.join();

	if(s->curl)
		curl_easy_cleanup(s->curl);
	free(s->cache);
	delete s;
}

extern "C" int netRead(NetStream *s, uint8_t *buf, int size)
{
	s->lock.lock();
	while(s->wrPos == s->rdPos && !s->eof && !s->quit && !s->failed)
		s->dataCond.wait(s->lock);

	if(s->quit || (s->wrPos == s->rdPos && (s->eof || s->failed)))
	{
		s->lock.unlock();
		return 0;
	}

	int64_t avail = s->wrPos - s->rdPos;
	if(avail <= 0)
	{
		s->lock.unlock();
		return 0;
	}

	int64_t cSize = s->cacheSize > 0 ? s->cacheSize : CACHE_SIZE;
	int n = (int)(avail < size ? avail : size);
	int64_t pos = s->rdPos % cSize;
	int first = n;
	if(first > cSize - pos)
		first = cSize - pos;

	memcpy(buf, s->cache + pos, first);
	if(n > first)
		memcpy(buf + first, s->cache, n - first);

	s->rdPos += n;
	s->spaceCond.signal();
	s->lock.unlock();

	return n;
}

extern "C" int64_t netSeek(NetStream *s, int64_t offset, int whence)
{
	s->lock.lock();
	int64_t cur = s->rdPos;
	int64_t total = s->total;
	s->lock.unlock();

	int64_t target = offset;
	if(whence == SEEK_CUR)
		target = cur + offset;
	else if(whence == SEEK_END)
	{
		if(total < 0)
			return -1;
		target = total + offset;
	}
	if(target < 0)
		return -1;

	s->lock.lock();
	if(target >= s->rdPos && target <= s->wrPos)
	{
		s->rdPos = target;
		s->spaceCond.signal();
	}
	else
	{
		s->rdPos = target;
		s->wrPos = target;
		s->eof = false;
		s->failed = false;
		s->restart = true;
		s->restartOffset = target;
		s->spaceCond.signal();
		s->dataCond.signal();
	}
	s->lock.unlock();

	return target;
}

extern "C" int64_t netSize(NetStream *s)
{
	s->lock.lock();
	int64_t t = s->total;
	s->lock.unlock();
	return t;
}
