#include "global.hpp"
#include "fileops.hpp"

#include <unistd.h>
#include <cmath>
#include <string.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <map>
#include <time.h>


using std::string;

struct ch_config global::config;
bool global::ERROR;
bool global::ALIVE;

pthread_mutex_t global::mutex;
pthread_cond_t global::cond;
pthread_t global::timer;
uint32_t global::waiting;

std::map<int64_t, ch_state> openChunks;
time_t now;
time_t lruTime;
time_t lastConfig;
time_t lastCleanup;
uint32_t cleanupDiv = 0;

uint32_t maxComplWritesSize = 0;

void global::INIT() {
	nbdkit_debug("global::INIT");
	global::ERROR = false;
	global::ALIVE = true;

	if(pthread_mutex_init(&mutex, NULL)) {
		throw "mutex_init failed!";
	}
	if(pthread_cond_init(&cond, NULL)) {
		throw "cond_init_failed!";
	}
	global::waiting = 0;

	now = time(0);
	lruTime = now;
	lastConfig = now;
	lastCleanup = now;

	strcpy(global::config.BASE_PATH, "./mount/");
	strcpy(global::config.EXPORT_NAME, "testExport");
}

void parseSizeParam(const char *key, const char *value, const char *expected, int64_t *to, int64_t min, int64_t max, int64_t def) {
	if(strcmp(key, expected) == 0) {
		int64_t oVal = *to;
		*to = nbdkit_parse_size(value);
		if(oVal != *to) nbdkit_error("%s changed %ld -> %ld", expected, oVal, *to);
	}
	if(*to < min) {
		nbdkit_error("%s too low (%ld), using default value %ld", expected, *to, def);
		*to = def;
	}
	if(*to > max) {
		nbdkit_error("%s too high (%ld), using default value %ld", expected, *to, def);
		*to = def;
	}
}

void do_apply_config(const char *key, const char *value) {
	parseSizeParam(key, value, "CHUNKED_NUM_CHUNKS", &global::config.NUM_CHUNKS, 1, 1024*1024, 4096);
	parseSizeParam(key, value, "CHUNKED_MAX_OPEN_FILES", &global::config.MAX_OPEN_FILES, 0, 2048, 128);
	parseSizeParam(key, value, "CHUNKED_MAX_READ_LINGER_MINUTES", &global::config.MAX_READ_LINGER_MINUTES, 0, 100800, 10);
	parseSizeParam(key, value, "CHUNKED_MAX_WRITE_LINGER_MINUTES", &global::config.MAX_WRITE_LINGER_MINUTES, 0, 100800, 240);
	parseSizeParam(key, value, "CHUNKED_JUST_TOO_OLD_MINUTES", &global::config.JUST_TOO_OLD_MINUTES, 0, 100800, 480);
	parseSizeParam(key, value, "CHUNKED_FULLWRITE_LINGER_MINUTES", &global::config.FULLWRITE_LINGER_MINUTES, 0, 100800, 5);


	if(strcmp(key, "CHUNKED_BASE_PATH") == 0) {
		char *oVal = global::config.BASE_PATH;
		strcpy(global::config.BASE_PATH, value);
		if(strcmp(oVal, global::config.BASE_PATH)) nbdkit_error("cache path changed %s -> %s", oVal, global::config.BASE_PATH);
	}

	if(strcmp(key, "CHUNKED_EXPORT_NAME") == 0) {
		char *oVal = global::config.EXPORT_NAME;
		strcpy(global::config.EXPORT_NAME, value);
		if(strcmp(oVal, global::config.EXPORT_NAME)) nbdkit_error("export name changed %s -> %s", oVal, global::config.EXPORT_NAME);
	}
}

void global::apply_config() {
	std::ifstream cFile ("chunked.conf");
	if (cFile.is_open()) {
		std::string line;
		while(getline(cFile, line)) {
			line.erase(std::remove_if(line.begin(), line.end(), isspace), line.end());
			if(line.empty() || line[0] == '#' ) {
				continue;
			}
			auto delimiterPos = line.find("=");
			string name = line.substr(0, delimiterPos);
			string value = line.substr(delimiterPos + 1);
			do_apply_config(name.c_str(), value.c_str());
		}
	}
	else {
		std::cerr << "Couldn't open config file for reading.\n";
	}
}

void cleanup() {
	//Iterate all for one time
	for(auto it=openChunks.begin(); it != openChunks.end(); ) {
		const ch_state& state = openChunks[it->first];
		if(state.busy == 0) {
			//Been open for too long, regardless of last op
			if(state.opened < now - global::config.JUST_TOO_OLD_MINUTES*60) {
				nbdkit_debug("Closing too old chunk %ld, opened=%ld, now=%ld, open=%ld, cw=%lu.", it->first, state.opened, now, openChunks.size(), state.complWrites.size());
				closeFile(state.fd);
				it = openChunks.erase(it);
				continue;
			}
			//Lingering after last read
			if(state.write == false && state.lastOp < now - global::config.MAX_READ_LINGER_MINUTES*60) {
				nbdkit_debug("Closing lingering (ro) chunk %ld, lastOp=%ld, now=%ld, open=%ld.", it->first, state.lastOp, now, openChunks.size());
				closeFile(state.fd);
				it = openChunks.erase(it);
				continue;
			}
			//Lingering after last write
			if(state.write == true && state.lastOp < now - global::config.MAX_WRITE_LINGER_MINUTES*60) {
				nbdkit_debug("Closing lingering (rw) chunk %ld, lastOp=%ld, now=%ld, open=%ld, cw=%lu.", it->first, state.lastOp, now, openChunks.size(), state.complWrites.size());
				closeFile(state.fd);
				it = openChunks.erase(it);
				continue;
			}
			//Written full
			if(state.writePointer >= CHUNKSIZE && state.lastOp < now - global::config.FULLWRITE_LINGER_MINUTES*60) {
				nbdkit_debug("Closing fully written chunk %ld, lastOp=%ld, now=%ld, open=%ld.", it->first, state.lastOp, now, openChunks.size());
				closeFile(state.fd);
				it = openChunks.erase(it);
				continue;
			}
		}
	++it;
	}

	int tries = 0;
	while(openChunks.size() > global::config.MAX_OPEN_FILES && tries++ < 4) {
		bool deleted = false;
		for(auto it=openChunks.begin();it != openChunks.end() && openChunks.size() > global::config.MAX_OPEN_FILES; ) {
			const ch_state& state = openChunks[it->first];
			if(state.busy == 0 && state.lastOp < lruTime) {
				nbdkit_debug("Closing oldish chunk %ld (write=%d), lastOp=%ld, lastCleaned=%ld, now=%ld, open=%ld, cw=%lu.", it->first, state.write, state.lastOp, lruTime, now, openChunks.size(), state.complWrites.size());
				closeFile(state.fd);
				it = openChunks.erase(it);
				deleted = true;
			} else {
				++it;
			}
		}
		if(!deleted && openChunks.size() > global::config.MAX_OPEN_FILES) {
			lruTime = lruTime + MAX((now-lruTime)/25, 1);
		}
	}
}

const ch_state& global::getChunkForRead(int64_t chunkId) {
	lock();

	ch_state& state = openChunks[chunkId];

	if(state.fd >= 0) {
		state.lastOp = now;
		++state.busy;
		unlock();
		return state;
	}

	now = time(0);
	int fd = openFileForRead(chunkId);
	if(fd >= 0) {
		nbdkit_debug("Opened existing chunk %ld for read, open=%ld.", chunkId, openChunks.size());
		state.fd = fd;
		state.opened = now;
		state.lastOp = now;
		state.write = false;
		++state.busy;
		unlock();
		return state;
	}

	fd = openFileForRW(chunkId);
	nbdkit_debug("Opened NEW chunk %ld \"for read\", open=%ld.", chunkId, openChunks.size());
	state.fd = fd;
	state.opened = now;
	state.lastOp = now;
	state.write = true;
	++state.busy;
	unlock();
	return state;
}

const ch_state& global::getChunkForWrite(int64_t chunkId, uint64_t wrOff, uint32_t wrLen) {
	lock();

	while(true) {
		ch_state& state = openChunks[chunkId];

		if(state.exclusive) {
			wait();
			continue;
		}

		if(wrOff < state.writePointer && chunkId >= 0) {
			state.exclusive = true;
			nbdkit_debug("RESET chunk %ld, wrOff=%lu, WP=%u", chunkId, wrOff, state.writePointer);
			if(state.busy > 0) nbdkit_debug("\t...still busy, waiting.");
			while(state.busy > 0) {
				wait();
				state = openChunks[chunkId];
			}
			if(state.fd >= 0) closeFile(state.fd);
			//TODO
			//XXX
			//FIXME
			//while debugging
			//unlinkFile(chunkId);
			openChunks.erase(chunkId);
			state = openChunks[chunkId];
			nbdkit_debug("\tdone.");
		}

		if(state.complWrites.find(wrOff) != state.complWrites.end()) {
			nbdkit_error("OVERWRITE");
		}
		state.complWrites[wrOff] = wrLen;
		if(state.complWrites.size() > maxComplWritesSize) {
			maxComplWritesSize = state.complWrites.size();
			nbdkit_debug("COMPLETED WRITES MAX SIZE %u", maxComplWritesSize);
		}

		auto it=state.complWrites.begin();
		while(it != state.complWrites.end() && it->first == state.writePointer) {
			state.writePointer += it->second;
			it = state.complWrites.erase(it);
		}


		if(state.fd >= 0 && state.write == true) {
			state.lastOp = now;
			++state.busy;
			unlock();
			return state;
		}

		if(state.busy > 0) {
			wait();
			continue;
		}

		if(state.fd >= 0) {
			closeFile(state.fd);
			nbdkit_debug("Closed RO chunk %ld, open=%ld.", chunkId, openChunks.size());
		}

		now = time(0);
		state.fd = openFileForRW(chunkId);
		uint64_t size = getFileSize(state.fd);
		nbdkit_debug("Opened chunk %ld for write, size=%lu, open=%ld.", chunkId, size, openChunks.size());
		state.writePointer = size;
		state.opened = now;
		state.lastOp = now;
		state.write = true;
		++state.busy;
		unlock();
		return state;
	}
}

void global::finishedOp(int64_t chunkId) {
	lock();
	ch_state& state = openChunks[chunkId];
	if(--state.busy == 0) {
		if((cleanupDiv = ++cleanupDiv % 2) == 0) {
			lastCleanup = now;
			cleanup();
		}
		notifyAll();
	}
	unlock();
}

void global::flush() {
	lock();
	for(auto it=openChunks.begin();it!=openChunks.end();++it) {
		const ch_state& state = openChunks[it->first];
		if(state.write == true) {
			flushFile(state.fd);
		}
	}
	unlock();
	nbdkit_debug("\tFlushed.");
}

void global::shutdown() {
	nbdkit_debug("global::shutdown");
	ALIVE = false;
	pthread_join(timer, NULL);
	lock();
	while(openChunks.size() > 0) {
		for(auto it=openChunks.begin(); it != openChunks.end(); ) {
			const ch_state& state = openChunks[it->first];
			if(state.busy == 0) {
				closeFile(state.fd);
				it = openChunks.erase(it);
			} else {
				++it;
			}
		}
		if(openChunks.size() > 0) {
			wait();
		}
	}
	unlock();

	nbdkit_debug("\tDone.");
}

void global::START() {
	if(pthread_create(&timer, NULL, &timer_main, NULL)) {
		throw "pthread_create failed!";
	}
}

void *global::timer_main(void *args) {
	nbdkit_debug("timer_main");
	while(ALIVE) {
		sleep(2);
		lock();
		now = time(0);
		if(now-lastConfig >= 120) {
			lastConfig = now;
			apply_config();
		}
		if(now - lastCleanup >= 8) {
			lastCleanup = now;
			cleanup();
		}
		unlock();
	}
	nbdkit_debug("timer thread exited");
	return NULL;
}
