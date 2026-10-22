#pragma once
#include <limits.h>

const char* guestPath(int dirfd, const char* path, char (&buffer)[PATH_MAX], bool follow = true);
const char* guestMkdirPath(int dirfd, const char* path, char (&buffer)[PATH_MAX]);
bool guestPhysicalPath(const char* path);
const char* guestRoot();
