#ifndef __GLOBAL_HPP_
#define	__GLOBAL_HPP_

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>
#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#define CHUNKSIZE 1073741824l
#define HEADERSIZE 33554432l

#include <pthread.h>

struct ch_config {
	char BASE_PATH[128];
	char EXPORT_NAME[128];
	int64_t NUM_CHUNKS;
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

private:
	static pthread_mutex_t mutex;
	static void do_apply_config(const char *key, const char *value);

	static inline void lock() {
		if(pthread_mutex_lock(&mutex)) {
			throw "PTHREAD_MUTEX_LOCK FAILED!";
		}
	}

	static inline void unlock() {
		if(pthread_mutex_unlock(&mutex)) {
			throw "PTHREAD_MUTEX_UNLOCK FAILED!";
		}
	}

};
#endif
