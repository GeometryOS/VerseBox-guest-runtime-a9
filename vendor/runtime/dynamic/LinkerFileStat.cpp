#include "LinkerFileStat.h"
#include "GuestPaths.h"
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

// Loader metadata uses kernel inode/size, without guest ownership xattr queries.
int linkerFileStat(int fd, struct stat* info) {
    return syscall(__NR_fstat, fd, info);
}

int linkerPathStat(const char* path, struct stat* info) {
    char buffer[PATH_MAX];
    const char* physical = guestPath(AT_FDCWD, path, buffer);
    return physical ? syscall(__NR_newfstatat, AT_FDCWD, physical, info, 0) : -1;
}
