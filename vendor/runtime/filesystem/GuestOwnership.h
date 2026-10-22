#pragma once
#include <sys/stat.h>

constexpr size_t guestOwnershipSize = 16;
void guestApplyOwnership(const char* physical, int descriptor, struct stat* info);
