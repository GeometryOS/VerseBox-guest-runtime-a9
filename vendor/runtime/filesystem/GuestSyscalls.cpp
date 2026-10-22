#include "GuestPaths.h"
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" long a10vm_kernel_syscall(long, long, long, long, long, long, long);
extern "C" int __openat(int, const char*, int, int);

extern "C" long a10vm_path_syscall(long number, long a, long b, long c, long d, long e, long f) {
    if (!guestRoot()) return a10vm_kernel_syscall(number, a, b, c, d, e, f);
    if (number == __NR_openat)
        return __openat(a, reinterpret_cast<const char*>(b), c, d);
    if (number == __NR_readlinkat)
        return readlinkat(a, reinterpret_cast<const char*>(b), reinterpret_cast<char*>(c), d);
    if (number == __NR_symlinkat) {
        char destination[PATH_MAX];
        const char* mapped = guestPath(b, reinterpret_cast<const char*>(c), destination, false);
        return mapped ? a10vm_kernel_syscall(number, a, b, reinterpret_cast<long>(mapped), d, e, f) : -1;
    }
    char first[PATH_MAX], second[PATH_MAX];
    long* path = &b;
    int directory = a;
    bool follow = true;
    switch (number) {
        case __NR_newfstatat: follow = !(d & AT_SYMLINK_NOFOLLOW); break;
        case __NR_utimensat:
            if (!b) return a10vm_kernel_syscall(number, a, b, c, d, e, f);
            follow = !(d & AT_SYMLINK_NOFOLLOW);
            break;
        case __NR_mkdirat:
        case __NR_unlinkat:
        case __NR_readlinkat:
        case __NR_renameat:
        case __NR_renameat2: follow = false; break;
        case __NR_statfs:
        case __NR_truncate:
        case __NR_chdir: path = &a; directory = AT_FDCWD; break;
        case __NR_inotify_add_watch: directory = AT_FDCWD; break;
    }
    const char* mapped = number == __NR_mkdirat
        ? guestMkdirPath(directory, reinterpret_cast<const char*>(*path), first)
        : guestPath(directory, reinterpret_cast<const char*>(*path), first, follow);
    if (!mapped) return -1;
    *path = reinterpret_cast<long>(mapped);
    if (number == __NR_renameat || number == __NR_renameat2) {
        mapped = guestPath(c, reinterpret_cast<const char*>(d), second, false);
        if (!mapped) return -1;
        d = reinterpret_cast<long>(mapped);
    }
    return a10vm_kernel_syscall(number, a, b, c, d, e, f);
}
