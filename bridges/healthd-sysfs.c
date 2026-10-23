/*
 * Android 9 healthd sysfs redirection.
 *
 * Android 9's healthd (and libhealthd) read the battery through the
 * /sys/class/power_supply/battery directory.  The guest path layer deliberately
 * keeps /sys on the host sysfs (see GuestPaths.cpp), and an untrusted app
 * cannot read the host's power_supply nodes, so the guest battery stays at
 * whatever healthd falls back to and never matches the real device.
 *
 * Android 10 solves this with the project's health@2.0 HAL, which reads the
 * host battery over the "battery" socket (HostBatteryBridge / HostHealthHal).
 * API 28 has no such HAL in this port, but its healthd reads regular files, so
 * the same host battery snapshot is materialised under the instance state root
 * and that prefix is substituted here.  Only /sys/class/power_supply is
 * touched; every other path is forwarded unchanged.
 *
 * The state root comes from A10VM_STATE_ROOT, which FrameworkSession already
 * exports to every guest process.  If it is absent the wrapper is inert.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#define GUEST_PREFIX "/sys/class/power_supply"
#define HOST_SUFFIX "/battery-sysfs/class/power_supply"

static const char* rewrite(const char* path, char* buffer, size_t size) {
    if (path == NULL || strncmp(path, GUEST_PREFIX, sizeof(GUEST_PREFIX) - 1) != 0) return path;
    const char* after = path + sizeof(GUEST_PREFIX) - 1;
    if (*after != '\0' && *after != '/') return path;
    const char* state = getenv("A10VM_STATE_ROOT");
    if (state == NULL || state[0] == '\0') return path;
    int written = snprintf(buffer, size, "%s%s%s", state, HOST_SUFFIX, after);
    return written > 0 && (size_t)written < size ? buffer : path;
}

static void note(const char* text) {
    size_t length = strlen(text);
    (void)!write(2, text, length);
}

int open(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    char buffer[4096];
    const char* target = rewrite(path, buffer, sizeof(buffer));
    static int (*real)(const char*, int, ...);
    if (real == NULL) real = (int (*)(const char*, int, ...))dlsym(RTLD_NEXT, "open");
    if (real == NULL) { errno = ENOSYS; return -1; }
    return real(target, flags, mode);
}

int open64(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    char buffer[4096];
    const char* target = rewrite(path, buffer, sizeof(buffer));
    static int (*real)(const char*, int, ...);
    if (real == NULL) real = (int (*)(const char*, int, ...))dlsym(RTLD_NEXT, "open64");
    if (real == NULL) real = (int (*)(const char*, int, ...))dlsym(RTLD_NEXT, "open");
    if (real == NULL) { errno = ENOSYS; return -1; }
    return real(target, flags, mode);
}

int openat(int dirfd, const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    char buffer[4096];
    const char* target = rewrite(path, buffer, sizeof(buffer));
    static int (*real)(int, const char*, int, ...);
    if (real == NULL) real = (int (*)(int, const char*, int, ...))dlsym(RTLD_NEXT, "openat");
    if (real == NULL) { errno = ENOSYS; return -1; }
    return real(dirfd, target, flags, mode);
}

int openat64(int dirfd, const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    char buffer[4096];
    const char* target = rewrite(path, buffer, sizeof(buffer));
    static int (*real)(int, const char*, int, ...);
    if (real == NULL) real = (int (*)(int, const char*, int, ...))dlsym(RTLD_NEXT, "openat64");
    if (real == NULL) real = (int (*)(int, const char*, int, ...))dlsym(RTLD_NEXT, "openat");
    if (real == NULL) { errno = ENOSYS; return -1; }
    return real(dirfd, target, flags, mode);
}

FILE* fopen(const char* path, const char* mode) {
    char buffer[4096];
    const char* target = rewrite(path, buffer, sizeof(buffer));
    static FILE* (*real)(const char*, const char*);
    if (real == NULL) real = (FILE* (*)(const char*, const char*))dlsym(RTLD_NEXT, "fopen");
    if (real == NULL) { errno = ENOSYS; return NULL; }
    return real(target, mode);
}

FILE* fopen64(const char* path, const char* mode) {
    char buffer[4096];
    const char* target = rewrite(path, buffer, sizeof(buffer));
    static FILE* (*real)(const char*, const char*);
    if (real == NULL) real = (FILE* (*)(const char*, const char*))dlsym(RTLD_NEXT, "fopen64");
    if (real == NULL) real = (FILE* (*)(const char*, const char*))dlsym(RTLD_NEXT, "fopen");
    if (real == NULL) { errno = ENOSYS; return NULL; }
    return real(target, mode);
}

int stat(const char* path, struct stat* info) {
    char buffer[4096];
    const char* target = rewrite(path, buffer, sizeof(buffer));
    static int (*real)(const char*, struct stat*);
    if (real == NULL) real = (int (*)(const char*, struct stat*))dlsym(RTLD_NEXT, "stat");
    if (real == NULL) { errno = ENOSYS; return -1; }
    return real(target, info);
}

int lstat(const char* path, struct stat* info) {
    char buffer[4096];
    const char* target = rewrite(path, buffer, sizeof(buffer));
    static int (*real)(const char*, struct stat*);
    if (real == NULL) real = (int (*)(const char*, struct stat*))dlsym(RTLD_NEXT, "lstat");
    if (real == NULL) { errno = ENOSYS; return -1; }
    return real(target, info);
}

int access(const char* path, int mode) {
    char buffer[4096];
    const char* target = rewrite(path, buffer, sizeof(buffer));
    static int (*real)(const char*, int);
    if (real == NULL) real = (int (*)(const char*, int))dlsym(RTLD_NEXT, "access");
    if (real == NULL) { errno = ENOSYS; return -1; }
    return real(target, mode);
}

DIR* opendir(const char* path) {
    char buffer[4096];
    const char* target = rewrite(path, buffer, sizeof(buffer));
    static DIR* (*real)(const char*);
    if (real == NULL) real = (DIR* (*)(const char*))dlsym(RTLD_NEXT, "opendir");
    if (real == NULL) { errno = ENOSYS; return NULL; }
    return real(target);
}

__attribute__((constructor)) static void loaded(void) {
    if (getenv("A10VM_STATE_ROOT") != NULL) note("a9vm_healthd_sysfs=active\n");
}
