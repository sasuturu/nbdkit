#ifndef __GLOBAL_HPP_
#define	__GLOBAL_HPP_

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>
#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#define CHUNKSIZE 1073741824l
#define DEFAULT_CHUNKS 4096
#define MAX_CHUNKS 1048576
#define MAX_OPEN_FILES 128

#include <pthread.h>
#include <map>

struct ch_config {
	char BASE_PATH[128];
	char EXPORT_NAME[128];
	int64_t NUM_CHUNKS;
};

struct ch_state {
	int fd;
	uint64_t lastOp;
	bool write;
	uint32_t busy;

	ch_state() {
		fd = -1;
		lastOp = 0;
		write = false;
		busy = 0;
	}
};

class global {
public:
	static void INIT();
	static void START();
	static void apply_config();
	static ch_state& getChunkForRead(uint32_t chunkId);
	static ch_state& getChunkForWrite(uint32_t chunkId);
	static void finishedOp(uint32_t chunkId);
	static void closeAllOpenFiles();
	static struct ch_config config;
	static bool ALIVE;
	static bool ERROR;

private:
	static pthread_mutex_t mutex;
	static pthread_cond_t cond;
	static std::map<uint32_t, ch_state> openChunks;
	static void do_apply_config(const char *key, const char *value);
	static void cleanup();

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

	static inline void wait() {
		if(pthread_cond_wait(&cond, &mutex)) {
			throw "pthread_cond_wait failed!";
		}
	}

	static inline void notifyAll() {
		if(pthread_cond_broadcast(&cond)) {
			throw "pthread_cond_broadcast failed!";
		}
	}

};
#endif
