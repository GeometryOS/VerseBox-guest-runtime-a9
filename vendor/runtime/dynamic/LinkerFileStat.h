#pragma once
#include <sys/stat.h>

int linkerFileStat(int fd, struct stat* info);
int linkerPathStat(const char* path, struct stat* info);
