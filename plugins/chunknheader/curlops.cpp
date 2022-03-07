#include "global.hpp"
#include "curlops.hpp"
#include "fileops.hpp"
#include <curl/curl.h>
#include <unistd.h>
#include <string.h>

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	ch_cctx *cctx = (struct ch_cctx *) userp;

	memcpy(&(cctx->buffer[cctx->size]), contents, realsize);
	cctx->size += realsize;

	return realsize;
}

size_t DiscardMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	return size * nmemb;
}

void resetCurl(struct ch_cctx *cctx) {
	curl_easy_cleanup(cctx->curl);
	cctx->curl = curl_easy_init();
}

void httpGet(uint32_t chunkId, char *buffer, size_t off, size_t len) {
	char relPath[128];
	toRelPath(relPath, chunkId);
	char URL[512];
	toAbsolutePath(URL, global::config.BASE_URL, relPath);
	nbdkit_debug("downloading %s %u %u %u", URL, chunkId, off, len);

	uint64_t waitUSec = 0;
	uint64_t addUSec = 0;
	uint64_t timeout = global::config.TIMEOUT_BASE;

	ch_cctx *cctx = global::getContext();
	cctx->buffer = buffer;

	while(true) {
		long http_code = 0;
		timeout = timeout * global::config.TIMEOUT_MUL / global::config.TIMEOUT_DIV + global::config.TIMEOUT_ADD;
		struct curl_slist *headers = NULL;
		headers = curl_slist_append(headers, "Accept: application/vnd.x.restic.rest.v2");
		char range[256];
		snprintf(range, 256, "Range: bytes=%u-%u", off, off+len-1);
		headers = curl_slist_append(headers, range);

		curl_easy_reset(cctx->curl);
		cctx->size = 0;
		curl_easy_setopt(cctx->curl, CURLOPT_URL, URL);
		curl_easy_setopt(cctx->curl, CURLOPT_HTTPGET, 1L);
		curl_easy_setopt(cctx->curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		curl_easy_setopt(cctx->curl, CURLOPT_WRITEDATA, cctx);
		curl_easy_setopt(cctx->curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(cctx->curl, CURLOPT_TIMEOUT_MS, timeout);
		//curl_easy_setopt(cctx->curl, CURLOPT_DEBUGFUNCTION, debug_callback);
		//curl_easy_setopt(cctx->curl, CURLOPT_VERBOSE, 1L);

		CURLcode res = curl_easy_perform(cctx->curl);
		curl_easy_getinfo (cctx->curl, CURLINFO_RESPONSE_CODE, &http_code);
		curl_slist_free_all(headers);

		if (res == CURLE_OK && (http_code == 200 || http_code == 206) && cctx->size == len) {
			//nbdkit_debug("downloaded %lu", blk);
			break;
		} else if ((res == CURLE_OK && http_code == 404) /*|| (res == CURLE_OK && http_code == 200 && cctx->size == 0)*/) {
			nbdkit_debug("read nonexisting %s, %d, %lu, %lu, %s", URL, res, http_code, cctx->size, curl_easy_strerror(res));
			//nbdkit_debug("downloaded %lu", blk);
			break;
		} else {
			addUSec += WAIT;
			waitUSec += addUSec;
			if(waitUSec < 3000000) {
				nbdkit_debug("unhandled read response, retrying: %i, %lu, %lu, %s", res, http_code, cctx->size, curl_easy_strerror(res));
				resetCurl(cctx);
				usleep(waitUSec);
			} else if (waitUSec < 1800000000) {
				nbdkit_error("unhandled read response, retrying: %i, %lu, %lu, %s", res, http_code, cctx->size, curl_easy_strerror(res));
				resetCurl(cctx);
				usleep(waitUSec);
			} else {
				nbdkit_error("unhandled read response: %i, %lu, %lu, %s", res, http_code, cctx->size, curl_easy_strerror(res));
				throw "Download failed!";
			}
		}
	}
	global::putContext(cctx);
}
