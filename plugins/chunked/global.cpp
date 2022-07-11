#include "global.hpp"

#include <unistd.h>
#include <cmath>
#include <string.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <map>

#include "fileops.hpp"

using std::string;

struct ch_config global::config;
bool global::ALIVE = true;
bool global::ERROR = false;

pthread_mutex_t global::mutex;
pthread_cond_t global::cond;
std::map<uint32_t, ch_state> global::openChunks;
uint64_t opCounter;
uint64_t lastCleanedOp;

void global::INIT() {
	nbdkit_debug("global::INIT");
	ALIVE = true;
	ERROR = false;

	if(pthread_mutex_init(&mutex, NULL)) {
		throw "mutex_init failed!";
	}
	if(pthread_cond_init(&cond, NULL)) {
		throw "cond_init_failed!";
	}

	opCounter = 0;
	lastCleanedOp = 0;

	global::config.NUM_CHUNKS = DEFAULT_CHUNKS;
	strcpy(global::config.BASE_PATH, "./mount/");
	strcpy(global::config.EXPORT_NAME, "testExport");
}

void global::START() {
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
			global::do_apply_config(name.c_str(), value.c_str());
		}
	}
	else {
		std::cerr << "Couldn't open config file for reading.\n";
	}
}

void global::do_apply_config(const char *key, const char *value) {
	if(strcmp(key, "CHUNKED_NUM_CHUNKS") == 0) {
		int64_t oVal = global::config.NUM_CHUNKS;
		global::config.NUM_CHUNKS = nbdkit_parse_size(value);
		if(oVal != global::config.NUM_CHUNKS) nbdkit_error("export size in chunks changed %ld -> %ld", oVal, global::config.NUM_CHUNKS);
	}
	if(global::config.NUM_CHUNKS <= 0) {
		nbdkit_error("Using default value %s for export size in chunks", DEFAULT_CHUNKS);
		global::config.NUM_CHUNKS = DEFAULT_CHUNKS;
	}
	if(global::config.NUM_CHUNKS > MAX_CHUNKS) {
		nbdkit_error("Too many chunks! Reverting to max of %s.", MAX_CHUNKS);
		global::config.NUM_CHUNKS = MAX_CHUNKS;
	}

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

ch_state& global::getChunkForRead(uint32_t chunkId) {
	lock();

	ch_state& state = openChunks[chunkId];

	if(state.fd >= 0) {
		state.lastOp = ++opCounter;
		++state.busy;
		cleanup();
		unlock();
		return state;
	}

	int fd = openForRead(chunkId);
	if(fd >= 0) {
		state.fd = fd;
		state.lastOp = ++opCounter;
		state.write = false;
		++state.busy;
		cleanup();
		unlock();
		return state;
	}

	fd = openForRW(chunkId);
	state.fd = fd;
	state.lastOp = ++opCounter;
	state.write = true;
	++state.busy;
	cleanup();
	unlock();
	return state;
}

ch_state& global::getChunkForWrite(uint32_t chunkId) {
	lock();

	ch_state& state = openChunks[chunkId];

	if(state.fd >= 0 && state.write == true) {
		state.lastOp = ++opCounter;
		++state.busy;
		cleanup();
		unlock();
		return state;
	}

	if(state.fd < 0) {
		state.fd = openForRW(chunkId);
		state.lastOp = ++opCounter;
		state.write = true;
		++state.busy;
		cleanup();
		unlock();
		return state;
	}

	while(state.busy > 0) {
		wait();
	}

	if(state.fd >= 0) closeFd(state.fd);
	state.fd = openForRW(chunkId);
	state.lastOp = ++opCounter;
	state.write = true;
	++state.busy;
	cleanup();
	unlock();
	return state;
}

void global::finishedOp(uint32_t chunkId) {
	lock();
	ch_state& state = openChunks[chunkId];
	if(--state.busy == 0) {
		notifyAll();
	}
	unlock();
}

void global::cleanup() {
	int tries = 0;
	while(openChunks.size() > MAX_OPEN_FILES && tries++ < 4) {
		lastCleanedOp = lastCleanedOp + (opCounter-lastCleanedOp)/10;
		for(auto it=openChunks.begin();it != openChunks.end(); ++it) {
			ch_state& state = it->second;
			if(state.busy == 0 && state.lastOp < lastCleanedOp) {
				openChunks.erase(it);
				break;
			}
		}
	}
}

void global::closeAllOpenFiles() {
	lock();
	while(openChunks.size() > 0) {
		for(auto it=openChunks.begin(); it != openChunks.end();) {
			while(it->second.busy > 0) {
				wait();
			}
			it = openChunks.erase(it);
		}
	}
	unlock();
}
