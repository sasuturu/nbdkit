#ifndef __FILEOPS_HPP_
#define	__FILEOPS_HPP_

#include <stdint.h>
#include <sys/types.h>

void toRelPath(char *str, int64_t chunkId);
void toAbsolutePath(char *dest, char *BASE, char *path);
void mkdir(char *PATH);

int openFileForRead(int64_t chunkId);
int openFileForRW(int64_t chunkId);
void readFile(int fd, char *buffer, off_t off, size_t len);
void writeFile(int fd, const char *buffer, off_t off, size_t len);
void closeFile(int fd);
void flushFile(int fd);

#endif
