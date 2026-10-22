#include "GuestPaths.h"
#include "GuestOwnership.h"
#include "GuestProcMaps.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/inotify.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" int __openat(int fd, const char* path, int flags, int mode) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(fd, path, buffer, !(flags & O_NOFOLLOW));
    if (!mapped) return -1;
    int result = syscall(__NR_openat, fd, mapped, flags, mode);
#ifndef LIBC_STATIC
    if (result >= 0) result = guestProcMaps(result, mapped, flags);
#endif
    return result;
}

extern "C" int ___faccessat(int fd, const char* path, int mode) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(fd, path, buffer);
    return mapped ? syscall(__NR_faccessat, fd, mapped, mode) : -1;
}

extern "C" int __statfs(const char* path, struct statfs* info) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer);
    return mapped ? syscall(__NR_statfs, mapped, info) : -1;
}

int truncate(const char* path, off_t length) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer);
    return mapped ? syscall(__NR_truncate, mapped, length) : -1;
}
__strong_alias(truncate64, truncate);

int fstatat(int fd, const char* path, struct stat* info, int flags) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(fd, path, buffer, !(flags & AT_SYMLINK_NOFOLLOW));
    if (!mapped) return -1;
    int result = syscall(__NR_newfstatat, fd, mapped, info, flags);
    if (result == 0) guestApplyOwnership(mapped, -1, info);
    return result;
}
__strong_alias(fstatat64, fstatat);

int utimensat(int fd, const char* path, const struct timespec times[2], int flags) {
    char buffer[PATH_MAX];
    const char* mapped = path ? guestPath(fd, path, buffer, !(flags & AT_SYMLINK_NOFOLLOW)) : nullptr;
    if (path && !mapped) return -1;
    return syscall(__NR_utimensat, fd, mapped, times, flags);
}

ssize_t readlinkat(int fd, const char* path, char* output, size_t size) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(fd, path, buffer, false);
    if (!mapped) return -1;
    if (!guestRoot()) return syscall(__NR_readlinkat, fd, mapped, output, size);
    if (!size) { errno = EINVAL; return -1; }
    if (!output) { errno = EFAULT; return -1; }
    // Resolve before truncating: /proc fd/cwd links contain the physical root.
    char target[PATH_MAX + 1];
    ssize_t count = syscall(__NR_readlinkat, fd, mapped, target, PATH_MAX);
    if (count < 0) return -1;
    target[count] = '\0';
    const char* logical = target;
    if (guestPhysicalPath(target)) {
        logical += strlen(guestRoot());
        if (!logical[0]) logical = "/";
        count = strlen(logical);
    }
    size_t length = static_cast<size_t>(count) < size ? static_cast<size_t>(count) : size;
    memcpy(output, logical, length);
    return length;
}

int chdir(const char* path) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer);
    return mapped ? syscall(__NR_chdir, mapped) : -1;
}

extern "C" int __getcwd(char* output, size_t size) {
    if (!guestRoot()) return syscall(__NR_getcwd, output, size);
    char buffer[PATH_MAX];
    int count = syscall(__NR_getcwd, buffer, sizeof(buffer));
    if (count < 0) return -1;
    const char* path = guestPhysicalPath(buffer) ? buffer + strlen(guestRoot()) : buffer;
    if (!path[0]) path = "/";
    size_t length = strlen(path) + 1;
    if (length > size) { errno = ERANGE; return -1; }
    memcpy(output, path, length);
    return length;
}

int mkdirat(int fd, const char* path, mode_t mode) {
    char buffer[PATH_MAX];
    const char* mapped = guestMkdirPath(fd, path, buffer);
    return mapped ? syscall(__NR_mkdirat, fd, mapped, mode) : -1;
}

int symlinkat(const char* target, int fd, const char* path) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(fd, path, buffer, false);
    // Keep the target in guest coordinates; guestPath resolves it on later access.
    return mapped ? syscall(__NR_symlinkat, target, fd, mapped) : -1;
}

int inotify_add_watch(int fd, const char* path, uint32_t mask) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer);
    return mapped ? syscall(__NR_inotify_add_watch, fd, mapped, mask) : -1;
}

int unlinkat(int fd, const char* path, int flags) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(fd, path, buffer, false);
    return mapped ? syscall(__NR_unlinkat, fd, mapped, flags) : -1;
}

int renameat(int oldfd, const char* oldpath, int newfd, const char* newpath) {
    char oldbuffer[PATH_MAX], newbuffer[PATH_MAX];
    const char* oldmapped = guestPath(oldfd, oldpath, oldbuffer, false);
    if (!oldmapped) return -1;
    const char* newmapped = guestPath(newfd, newpath, newbuffer, false);
    return newmapped ? syscall(__NR_renameat, oldfd, oldmapped, newfd, newmapped) : -1;
}
