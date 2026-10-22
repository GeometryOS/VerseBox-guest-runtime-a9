#include "GuestPaths.h"
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>

int setxattr(const char* path, const char* name, const void* value, size_t size, int flags) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer);
    return mapped ? syscall(__NR_setxattr, mapped, name, value, size, flags) : -1;
}

int lsetxattr(const char* path, const char* name, const void* value, size_t size, int flags) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer, false);
    return mapped ? syscall(__NR_lsetxattr, mapped, name, value, size, flags) : -1;
}

ssize_t getxattr(const char* path, const char* name, void* value, size_t size) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer);
    return mapped ? syscall(__NR_getxattr, mapped, name, value, size) : -1;
}

ssize_t lgetxattr(const char* path, const char* name, void* value, size_t size) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer, false);
    return mapped ? syscall(__NR_lgetxattr, mapped, name, value, size) : -1;
}

ssize_t listxattr(const char* path, char* names, size_t size) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer);
    return mapped ? syscall(__NR_listxattr, mapped, names, size) : -1;
}

ssize_t llistxattr(const char* path, char* names, size_t size) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer, false);
    return mapped ? syscall(__NR_llistxattr, mapped, names, size) : -1;
}

int removexattr(const char* path, const char* name) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer);
    return mapped ? syscall(__NR_removexattr, mapped, name) : -1;
}

int lremovexattr(const char* path, const char* name) {
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, path, buffer, false);
    return mapped ? syscall(__NR_lremovexattr, mapped, name) : -1;
}
