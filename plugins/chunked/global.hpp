#ifndef __GLOBAL_HPP_
#define	__GLOBAL_HPP_

#define NBDKIT_API_VERSION 2
#include <nbdkit-plugin.h>
#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL

#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#define CHUNKSIZE 268435456l
#define HEADERSIZE 33554432l

#include <pthread.h>
#include <map>

struct ch_config {
	char BASE_PATH[128];
	char EXPORT_NAME[128];
	int64_t NUM_CHUNKS;
	int64_t MAX_OPEN_FILES;
	int64_t MAX_LINGER_MINUTES;
	int64_t JUST_TOO_OLD_MINUTES;
	int64_t FULLWRITE_LINGER_MINUTES;
};

struct ch_state {
	int fd;
	bool write;
	uint32_t busy;

	time_t opened;
	time_t lastOp;
	uint32_t written;

	uint32_t writePointer;

	ch_state() {
		fd = -1;
		write = false;
		busy = 0;
		opened = 0;
		lastOp = 0;
		written = 0;
		writePointer = 0;
	}
};

class global {
public:
	static void INIT();
	static void START();
	static void apply_config();
	static ch_state& getChunkForRead(int64_t chunkId);
	static ch_state& getChunkForWrite(int64_t chunkId);
	static void finishedOp(int64_t chunkId, uint32_t bytesWritten);
	static void closeAllOpenFiles();
	static void shutdown();
	static struct ch_config config;
	static bool ERROR;
	static bool ALIVE;

private:
	static pthread_mutex_t mutex;
	static pthread_cond_t cond;
	static pthread_t timer;
	static void *timer_main(void *args);

	static uint32_t waiting;

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
		++waiting;
		if(pthread_cond_wait(&cond, &mutex)) {
			throw "pthread_cond_wait failed!";
		}
		--waiting;
	}

	static inline void notifyAll() {
		if(waiting > 0) {
			if(pthread_cond_broadcast(&cond)) {
				throw "pthread_cond_broadcast failed!";
			}
		}
	}
};
#endif
