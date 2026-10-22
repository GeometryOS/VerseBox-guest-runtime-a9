#include "HostDeviceTransport.h"
#include <cutils/ashmem.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/ashmem.h>
#include <linux/memfd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
constexpr int futureWrite = 0x0010;

bool modernSeals() {
    static const bool supported = [] {
        int fd = syscall(__NR_memfd_create, "a10vm-seal-probe", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (fd < 0) return false;
        bool result = fcntl(fd, F_ADD_SEALS, futureWrite) == 0;
        close(fd);
        return result;
    }();
    return supported;
}

template<typename Function> Function original(const char* name) {
    return reinterpret_cast<Function>(dlsym(RTLD_NEXT, name));
}
}

extern "C" int ashmem_create_region(const char* name, size_t size) {
    if (!getenv("A10VM_ROOTFS")) {
        auto call = original<decltype(&ashmem_create_region)>("ashmem_create_region");
        if (!call) { errno = ENOSYS; return -1; }
        return call(name, size);
    }
    if (size > INT_MAX) { errno = EOVERFLOW; return -1; }
    if (!modernSeals()) {
        HostDeviceSocket host("shared-memory", 0x4131304d, 1);
        if (!host.valid() || !host.put(size) ||
                !host.putString(std::string(name ? name : "a10vm", name ? strnlen(name, 255) : 5))) {
            errno = EIO;
            return -1;
        }
        return host.getDescriptor();
    }
    int fd = syscall(__NR_memfd_create, name ? name : "a10vm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) == 0) return fd;
    int error = errno;
    close(fd);
    errno = error;
    return -1;
}

extern "C" int ashmem_valid(int fd) {
    if (fcntl(fd, F_GET_SEALS) >= 0) return 1;
    return ioctl(fd, ASHMEM_GET_SIZE, 0) >= 0;
}

extern "C" int ashmem_get_size_region(int fd) {
    if (fcntl(fd, F_GET_SEALS) < 0) return ioctl(fd, ASHMEM_GET_SIZE, 0);
    struct stat info{};
    if (fstat(fd, &info)) return -1;
    if (info.st_size > INT_MAX) { errno = EOVERFLOW; return -1; }
    return info.st_size;
}

extern "C" int ashmem_set_prot_region(int fd, int protection) {
    int seals = fcntl(fd, F_GET_SEALS);
    if (seals < 0) return ioctl(fd, ASHMEM_SET_PROT_MASK, protection);
    if (protection & PROT_WRITE) {
        if (seals & (futureWrite | F_SEAL_WRITE)) { errno = EPERM; return -1; }
        return 0;
    }
    return fcntl(fd, F_ADD_SEALS, futureWrite);
}

extern "C" int ashmem_pin_region(int fd, size_t offset, size_t length) {
    if (fcntl(fd, F_GET_SEALS) >= 0) return 0;
    ashmem_pin pin{static_cast<uint32_t>(offset), static_cast<uint32_t>(length)};
    return ioctl(fd, ASHMEM_PIN, &pin);
}

extern "C" int ashmem_unpin_region(int fd, size_t offset, size_t length) {
    if (fcntl(fd, F_GET_SEALS) >= 0) return 0;
    ashmem_pin pin{static_cast<uint32_t>(offset), static_cast<uint32_t>(length)};
    return ioctl(fd, ASHMEM_UNPIN, &pin);
}
