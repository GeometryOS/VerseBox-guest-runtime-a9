#include "GuestPaths.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" char* a10vm_host_realpath(const char*, char*);
extern "C" int a10vm_host_lstat(const char* path, struct stat* info) {
    return syscall(__NR_newfstatat, AT_FDCWD, path, info, AT_SYMLINK_NOFOLLOW);
}
extern "C" ssize_t a10vm_host_readlink(const char* path, char* output, size_t size) {
    return syscall(__NR_readlinkat, AT_FDCWD, path, output, size);
}
extern "C" char* a10vm_host_getcwd(char* output, size_t size) {
    return syscall(__NR_getcwd, output, size) < 0 ? nullptr : output;
}

char* realpath(const char* path, char* resolved) {
    char mapped[PATH_MAX], canonical[PATH_MAX];
    const char* physical = guestPath(AT_FDCWD, path, mapped);
    if (!physical || !a10vm_host_realpath(physical, canonical)) return nullptr;
    const char* visible = guestPhysicalPath(canonical) ? canonical + strlen(guestRoot()) : canonical;
    if (!*visible) visible = "/";
    if (!resolved) return strdup(visible);
    return strcpy(resolved, visible);
}
