#ifndef __GLOBAL_HPP_
#define	__GLOBAL_HPP_

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>
#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#define CHUNKSIZE 1073741824l
#define HEADERSIZE 33554432l
#define WAIT 50000

#include "lru.hpp"
#include "synclist.hpp"
#include "chunk.hpp"
#include <curl/curl.h>

struct ch_config {
	char BASE_URL[128];
	char BASE_PATH[128];
	char EXPORT_NAME[128];
	int64_t EXPORT_SIZE;

	int64_t TIMEOUT_BASE;
	int64_t TIMEOUT_MUL;
	int64_t TIMEOUT_DIV;
	int64_t TIMEOUT_ADD;
};

class ch_cctx {
public:
	CURL *curl;
	size_t size;
	char *buffer;
};

class global {
public:
	static void INIT();
	static void START();
	static void apply_config();
	static void printStats();
	static struct ch_config config;
	static bool ALIVE;
	static bool ERROR;

	static ch_cctx* getContext();
	static void putContext(ch_cctx *cctx);
private:
	static pthread_mutex_t cctxm;
	static std::list<ch_cctx*> contexts;
	static void do_apply_config(const char *key, const char *value);

	static inline void lock(pthread_mutex_t *mutex) {
		if(pthread_mutex_lock(mutex)) {
			throw "PTHREAD_MUTEX_LOCK FAILED!";
		}
	}

	static inline void unlock(pthread_mutex_t *mutex) {
		if(pthread_mutex_unlock(mutex)) {
			throw "PTHREAD_MUTEX_UNLOCK FAILED!";
		}
	}

};
#endif
