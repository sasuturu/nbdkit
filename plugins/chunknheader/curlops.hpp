#ifndef __CURLOPS_HPP_
#define	__CURLOPS_HPP_

#include <stdint.h>
#include <sys/types.h>

void httpGet(uint32_t chunkId, char *buffer, size_t off, size_t len);
//void httpPost(uint32_t chunkId, char *buffer);

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
size_t DiscardMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);

#endif
