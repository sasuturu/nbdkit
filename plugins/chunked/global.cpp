#include "../chunked/global.hpp"

#include <unistd.h>
#include <cmath>
#include <string.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <map>

using std::string;

struct ch_config global::config;
bool global::ALIVE = true;
bool global::ERROR = false;

pthread_mutex_t global::mutex;

void global::INIT() {
	nbdkit_debug("global::INIT");
	ALIVE = true;
	ERROR = false;

	if(pthread_mutex_init(&mutex, NULL)) {
		throw "mutex_init failed!";
	}

	global::config.NUM_CHUNKS = 4096;
	strcpy(global::config.BASE_PATH, "./mount/");
	strcpy(global::config.EXPORT_NAME, "testExport");
}

void global::START() {
}

void global::apply_config() {
	std::ifstream cFile ("chunknheader.conf");
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
	if(strcmp(key, "CHUNKNHEADER_NUM_CHUNKS") == 0) {
		int64_t oVal = global::config.NUM_CHUNKS;
		global::config.NUM_CHUNKS = nbdkit_parse_size(value);
		if(oVal != global::config.NUM_CHUNKS) nbdkit_error("export size in chunks changed %ld -> %ld", oVal, global::config.NUM_CHUNKS);
	}
	if(global::config.NUM_CHUNKS <= 0) {
		nbdkit_error("Using default value 4096 for export size in chunks");
		global::config.NUM_CHUNKS = 4096;
	}

	if(strcmp(key, "CHUNKNHEADER_BASE_PATH") == 0) {
		char *oVal = global::config.BASE_PATH;
		strcpy(global::config.BASE_PATH, value);
		if(strcmp(oVal, global::config.BASE_PATH)) nbdkit_error("cache path changed %s -> %s", oVal, global::config.BASE_PATH);
	}

	if(strcmp(key, "CHUNKNHEADER_EXPORT_NAME") == 0) {
		char *oVal = global::config.EXPORT_NAME;
		strcpy(global::config.EXPORT_NAME, value);
		if(strcmp(oVal, global::config.EXPORT_NAME)) nbdkit_error("export name changed %s -> %s", oVal, global::config.EXPORT_NAME);
	}
}
