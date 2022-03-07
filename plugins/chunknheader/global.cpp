#include "global.hpp"
#include "chunk.hpp"
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

pthread_mutex_t global::cctxm;
std::list<ch_cctx*> global::contexts;

void global::INIT() {
	nbdkit_debug("global::INIT");
	ALIVE = true;
	ERROR = false;

	if(pthread_mutex_init(&cctxm, NULL)) {
		throw "mutex_init failed!";
	}

	global::config.EXPORT_SIZE = ((uint64_t)32)*1024*1024*1024*1024;
	strcpy(global::config.BASE_PATH, "./cache/");
	strcpy(global::config.BASE_URL, "http://127.0.0.1:8080/");
	strcpy(global::config.EXPORT_NAME, "testExport");
	global::config.TIMEOUT_BASE = 1500;
	global::config.TIMEOUT_MUL = 2;
	global::config.TIMEOUT_DIV = 1;
	global::config.TIMEOUT_ADD = 0;
}

void global::START() {
}

ch_cctx* global::getContext() {
	ch_cctx *res = NULL;
	lock(&cctxm);
	if(contexts.size() > 0) {
		res = contexts.front();
		contexts.pop_front();
	} else {
		res = new ch_cctx();
		res->curl = curl_easy_init();
		res->size = 0;
		res->buffer = NULL;
	}
	unlock(&cctxm);
	return res;
}

void global::putContext(ch_cctx *cctx) {
	cctx->size = 0;
	cctx->buffer = NULL;
	lock(&cctxm);
	contexts.push_back(cctx);
	unlock(&cctxm);
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
	if(strcmp(key, "CHUNKNHEADER_EXPORT_SIZE") == 0) {
		uint64_t oVal = global::config.EXPORT_SIZE;
		global::config.EXPORT_SIZE = nbdkit_parse_size(value);
		if(oVal != global::config.EXPORT_SIZE) nbdkit_error("export size changed %ld -> %ld", oVal, global::config.EXPORT_SIZE);
	}
	if(global::config.EXPORT_SIZE <= 0) {
		nbdkit_error("Using default value 32T for export size");
		global::config.EXPORT_SIZE = ((int64_t)32)*1024*1024*1024*1024;
	}

	if(strcmp(key, "CHUNKNHEADER_BASE_PATH") == 0) {
		char *oVal = global::config.BASE_PATH;
		strcpy(global::config.BASE_PATH, value);
		if(strcmp(oVal, global::config.BASE_PATH)) nbdkit_error("cache path changed %s -> %s", oVal, global::config.BASE_PATH);
	}

	if(strcmp(key, "CHUNKNHEADER_URL") == 0) {
		char *oVal = global::config.BASE_URL;
		strcpy(global::config.BASE_URL, value);
		if(strcmp(oVal, global::config.BASE_URL)) nbdkit_error("restic url changed %s -> %s", oVal, global::config.BASE_URL);
	}

	if(strcmp(key, "CHUNKNHEADER_EXPORT_NAME") == 0) {
		char *oVal = global::config.EXPORT_NAME;
		strcpy(global::config.EXPORT_NAME, value);
		if(strcmp(oVal, global::config.EXPORT_NAME)) nbdkit_error("export name changed %s -> %s", oVal, global::config.EXPORT_NAME);
	}

	if(strcmp(key, "TIMEOUT_BASE") == 0) {
		int64_t oVal = global::config.TIMEOUT_BASE;
		global::config.TIMEOUT_BASE = (int64_t) nbdkit_parse_size(value);
		if(oVal != global::config.TIMEOUT_BASE) nbdkit_error("timeout_base changed %ld -> %ld", oVal, global::config.TIMEOUT_BASE);
	}
	if(strcmp(key, "TIMEOUT_MUL") == 0) {
		int64_t oVal = global::config.TIMEOUT_MUL;
		global::config.TIMEOUT_MUL = (int64_t) nbdkit_parse_size(value);
		if(oVal != global::config.TIMEOUT_MUL) nbdkit_error("timeout_mul changed %ld -> %ld", oVal, global::config.TIMEOUT_MUL);
	}
	if(strcmp(key, "TIMEOUT_DIV") == 0) {
		int64_t oVal = global::config.TIMEOUT_DIV;
		global::config.TIMEOUT_DIV = (int64_t) nbdkit_parse_size(value);
		if(oVal != global::config.TIMEOUT_DIV) nbdkit_error("timeout_div changed %ld -> %ld", oVal, global::config.TIMEOUT_DIV);
	}
	if(strcmp(key, "TIMEOUT_ADD") == 0) {
		int64_t oVal = global::config.TIMEOUT_ADD;
		global::config.TIMEOUT_ADD = (int64_t) nbdkit_parse_size(value);
		if(oVal != global::config.TIMEOUT_ADD) nbdkit_error("timeout_add changed %ld -> %ld", oVal, global::config.TIMEOUT_ADD);
	}
}
