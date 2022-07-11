#ifndef __FILEOPS_HPP_
#define	__FILEOPS_HPP_

#include <stdint.h>
#include <sys/types.h>

void toRelPath(char *str, uint32_t chunkId);
void toAbsolutePath(char *dest, char *BASE, char *path);
void mkdir(char *PATH);

int openForRead(uint32_t chunkId);
int openForRW(uint32_t chunkId);
void readFile(int fd, char *buffer, off_t off, size_t len);
void writeFile(int fd, const char *buffer, off_t off, size_t len);
void closeFd(int fd);

#endif
