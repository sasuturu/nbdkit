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
time_t lastCleaned;
time_t lastConfig;

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
	lastCleaned = now;
	lastConfig = now;

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
	parseSizeParam(key, value, "CHUNKED_MAX_LINGER_MINUTES", &global::config.MAX_LINGER_MINUTES, 0, 100800, 60);
	parseSizeParam(key, value, "CHUNKED_JUST_TOO_OLD_MINUTES", &global::config.JUST_TOO_OLD_MINUTES, 0, 100800, 120);
	parseSizeParam(key, value, "CHUNKED_FULLWRITE_LINGER_MINUTES", &global::config.FULLWRITE_LINGER_MINUTES, 0, 100800, 10);


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
		ch_state& state = it->second;
		if(state.busy == 0) {
			//Been open for too long, regardless of last op
			if(state.opened < now - global::config.JUST_TOO_OLD_MINUTES*60) {
				nbdkit_debug("Closing too old chunk %ld, opened=%ld, now=%ld, open=%ld.", it->first, state.opened, now, openChunks.size());
				closeFd(state.fd);
				it = openChunks.erase(it);
				continue;
			}
			//Lingering after last op
			if(state.lastOp < now - global::config.MAX_LINGER_MINUTES*60) {
				nbdkit_debug("Closing lingering chunk %ld, lastOp=%ld, now=%ld, open=%ld.", it->first, state.lastOp, now, openChunks.size());
				closeFd(state.fd);
				it = openChunks.erase(it);
				continue;
			}
			//Written full
			if(state.written >= CHUNKSIZE && state.lastOp < now - global::config.FULLWRITE_LINGER_MINUTES*60) {
				nbdkit_debug("Closing fully written chunk %ld, lastOp=%ld, now=%ld, open=%ld.", it->first, state.lastOp, now, openChunks.size());
				closeFd(state.fd);
				it = openChunks.erase(it);
				continue;
			}
		}
	++it;
	}

	int tries = 0;
	while(openChunks.size() > global::config.MAX_OPEN_FILES && tries++ < 4) {
		bool deleted = false;
		for(auto it=openChunks.begin();it != openChunks.end(); ++it) {
			ch_state& state = it->second;
			if(state.busy == 0 && state.lastOp < lastCleaned) {
				nbdkit_debug("Closing oldish chunk %ld (write=%d), lastOp=%ld, lastCleaned=%ld, now=%ld, open=%ld.", it->first, state.write, state.lastOp, lastCleaned, now, openChunks.size());
				closeFd(state.fd);
				openChunks.erase(it);
				deleted = true;
				break;
			}
		}
		if(!deleted && openChunks.size() > global::config.MAX_OPEN_FILES) {
			lastCleaned = lastCleaned + (now-lastCleaned)/20;
		}
	}
}

ch_state& global::getChunkForRead(int64_t chunkId) {
	lock();

	ch_state& state = openChunks[chunkId];

	if(state.fd >= 0) {
		state.lastOp = now;
		++state.busy;
		unlock();
		return state;
	}

	now = time(0);
	int fd = openForRead(chunkId);
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

	fd = openForRW(chunkId);
	nbdkit_debug("Opened NEW chunk %ld \"for read\", open=%ld.", chunkId, openChunks.size());
	state.fd = fd;
	state.opened = now;
	state.lastOp = now;
	state.write = true;
	++state.busy;
	unlock();
	return state;
}

ch_state& global::getChunkForWrite(int64_t chunkId) {
	lock();

	while(true) {
		ch_state& state = openChunks[chunkId];

		if(state.fd >= 0 && state.write == true) {
			state.lastOp = now;
			++state.busy;
			unlock();
			return state;
		}

		now = time(0);
		if(state.fd < 0) {
			state.fd = openForRW(chunkId);
			nbdkit_debug("Opened chunk %ld for write, open=%ld.", chunkId, openChunks.size());
			state.opened = now;
			state.lastOp = now;
			state.write = true;
			++state.busy;
			unlock();
			return state;
		}

		if(state.busy > 0) {
			wait();
			continue;
		}

		if(state.fd >= 0) {
			closeFd(state.fd);
			nbdkit_debug("Closed RO chunk %ld, open=%ld.", chunkId, openChunks.size());
		}
		state.fd = openForRW(chunkId);
		nbdkit_debug("\t...and opened chunk %ld for writing, open=%ld.", chunkId, openChunks.size());
		state.opened = now;
		state.lastOp = now;
		state.write = true;
		++state.busy;
		unlock();
		return state;
	}
}

void global::finishedOp(int64_t chunkId, uint32_t bytesWritten) {
	lock();
	ch_state& state = openChunks[chunkId];
	state.written += bytesWritten;
	if(--state.busy == 0) {
		cleanup();
		notifyAll();
	}
	unlock();
}

void global::closeAllOpenFiles() {
	lock();
	while(openChunks.size() > 0) {
		for(auto it=openChunks.begin(); it != openChunks.end(); ) {
			ch_state& state = it->second;
			if(state.busy == 0) {
				closeFd(state.fd);
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
	nbdkit_debug("\tAll open chunks closed.");
}

void global::shutdown() {
	nbdkit_debug("global::shutdown");
	ALIVE = false;
	pthread_join(timer, NULL);
	nbdkit_debug("\tDone.");
}

void global::START() {
	if(pthread_create(&timer, NULL, &timer_main, NULL)) {
		throw "pthread_create failed!";
	}
}

void *global::timer_main(void *args) {
	nbdkit_debug("timer_main");
	uint32_t counter = 0;
	while(ALIVE) {
		sleep(2);
		lock();
		now = time(0);
		if(now-lastConfig >= 120) {
			apply_config();
			lastConfig = now;
		}
		if((counter = ++counter % 4) == 0) {
			cleanup();
		}
		unlock();
	}
	nbdkit_debug("timer thread exited");
	return NULL;
}
