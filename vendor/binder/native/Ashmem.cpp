#include <cutils/ashmem.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>

extern "C" int ashmem_create_region(const char* name, size_t size) {
    int fd = syscall(__NR_memfd_create, name ? name : "a10vm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) == 0) return fd;
    int error = errno;
    close(fd);
    errno = error;
    return -1;
}

extern "C" int ashmem_valid(int fd) {
    return fcntl(fd, F_GET_SEALS) >= 0;
}

extern "C" int ashmem_get_size_region(int fd) {
    struct stat info;
    if (!ashmem_valid(fd) || fstat(fd, &info) != 0) return -1;
    if (info.st_size > INT_MAX) { errno = EOVERFLOW; return -1; }
    return info.st_size;
}

extern "C" int ashmem_set_prot_region(int fd, int protection) {
    if (!ashmem_valid(fd)) { errno = EBADF; return -1; }
    if (protection != PROT_READ && protection != (PROT_READ | PROT_WRITE)) {
        errno = ENOTSUP;
        return -1;
    }
    int seals = fcntl(fd, F_GET_SEALS);
    if (protection & PROT_WRITE) {
        if (seals & 0x0010) { errno = EPERM; return -1; }
        return 0;
    }
    return fcntl(fd, F_ADD_SEALS, 0x0010);
}

extern "C" int ashmem_pin_region(int, size_t, size_t) { errno = ENOTSUP; return -1; }
extern "C" int ashmem_unpin_region(int, size_t, size_t) { errno = ENOTSUP; return -1; }
