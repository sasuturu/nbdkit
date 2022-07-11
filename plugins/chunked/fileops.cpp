#include "../chunked/fileops.hpp"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "../chunked/global.hpp"

void toRelPath(char *str, uint32_t chunkId) {
	snprintf(str, 128, "%09o", chunkId);
	for (int i = 0; i < strlen(str); ++i) {
		if (str[i] == 0) {
			nbdkit_debug("SHOULD NOT GET HERE!");
			break;
		}
		if (i % 4 == 3) {
			memmove(&str[i + 1], &str[i], 128 - i - 2);
			str[i] = '/';
		}
	}
	memcpy(&str[strlen(str)], ".bin", 5);
}

void toAbsolutePath(char *dest, char *BASE, char *path) {
	strcpy(dest, BASE);
	strcat(dest, global::config.EXPORT_NAME);
	strcat(dest, "/");
	strcat(dest, path);
}

void mkdir(char *PATH) {
	//nbdkit_debug("mkdir %s", PATH);
	struct stat st;
	char path[512];
	strcpy(path, PATH);
	for (int i=strlen(path);i>0;--i) {
		if(path[i] == '/') {
			path[i] = 0;
			if(stat(path, &st)) {
				nbdkit_debug("\tnot found (stat) %s", path);
				if(mkdir(path, 0771)) {
					nbdkit_debug("\tcouldn't create (mkdir) %s", path);
					mkdir(path);
					if(stat(path, &st)) {
						nbdkit_debug("\tnot found (stat2) %s", path);
						if(mkdir(path, 0771)) {
							nbdkit_error("COULDN'T MAKE PATH %s %s", path, PATH);
						}
					}
				}
			}
		}
	}
}

int openExistingFile(uint32_t chunkId) {
	char relPath[128];
	toRelPath(relPath, chunkId);
	char absPath[512];
	toAbsolutePath(absPath, global::config.BASE_PATH, relPath);

	return open(absPath, (O_RDWR), 0660);
}

int openNewFile(uint32_t chunkId) {
	//nbdkit_debug("openNewFile %lu", chunkId);
	char relPath[128];
	toRelPath(relPath, chunkId);
	char absPath[512];
	toAbsolutePath(absPath, global::config.BASE_PATH, relPath);

	int file = open(absPath, (O_RDWR | O_CREAT), 0660);
	if (file == -1) {
		nbdkit_debug("attempt to create path %s", absPath);
		mkdir(absPath);
		file = open(absPath, (O_RDWR | O_CREAT), 0660);
	}
	if (file == -1) {
		nbdkit_error("FAILED TO OPEN FILE %s: %d %s", absPath, errno, strerror(errno));
		throw "FAILED TO OPEN FILE";
	}
	if(ftruncate(file, CHUNKSIZE)) {
		nbdkit_error("FTRUNCATE FAILED %s: %d %s", absPath, errno, strerror(errno));
		throw "FTRUNCATE FAILED";
	}
	return file;
}

void readFile(int fd, char *buf, off_t off, size_t len) {
	//nbdkit_debug("readFile");
	while (len > 0) {
		ssize_t r = pread (fd, buf, len, off);
		if (r == -1) {
			nbdkit_error ("COULDN'T READ FILE, fd: %d, err: %d, errstr: %s", fd, errno, strerror(errno));
			throw "FILE PREAD FAILED";
		}
		if (r == 0) {
			nbdkit_error ("UNEXPECTED EOF!");
			throw "UNEXPECTED EOF";
		}
		buf += r;
		len -= r;
		off += r;
	}
}

void writeFile(int fd, const char *buf, off_t off, size_t len) {
	//nbdkit_debug("writeFile");
	while (len > 0) {
		ssize_t r = pwrite (fd, buf, len, off);
		if (r == -1) {
			nbdkit_error("FAILED TO WRITE TO FILE, fd: %d, err: %d, errstr: %s", fd, errno, strerror(errno));
			throw "FILE PWRITE FAILED";
		}
		buf += r;
		len -= r;
		off += r;
	}
}

void closeFd(int fd) {
	//nbdkit_debug("closeFd");
	if(close(fd)) {
		throw "close file failed!";
	}
}
