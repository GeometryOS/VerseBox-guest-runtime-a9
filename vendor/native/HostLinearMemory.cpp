#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
template<typename Function> Function original(const char* name) {
    return reinterpret_cast<Function>(dlsym(RTLD_NEXT, name));
}
constexpr unsigned closeOnExec = 1;
}

extern "C" int ion_open() {
    if (!getenv("A10VM_ROOTFS")) {
        auto call = original<int (*)()>("ion_open");
        if (!call) { errno = ENOSYS; return -1; }
        return call();
    }
    // Codec2's modern ION API uses shared fds; CPU codecs can use coherent memfd pages.
    return syscall(__NR_memfd_create, "a10vm-codec-client", closeOnExec);
}

extern "C" int ion_is_legacy(int fd) {
    if (getenv("A10VM_ROOTFS")) return 0;
    auto call = original<int (*)(int)>("ion_is_legacy");
    return call ? call(fd) : 1;
}

extern "C" int ion_alloc_fd(int fd, size_t size, size_t alignment, unsigned heaps,
        unsigned flags, int* output) {
    if (!getenv("A10VM_ROOTFS")) {
        auto call = original<decltype(&ion_alloc_fd)>("ion_alloc_fd");
        return call ? call(fd, size, alignment, heaps, flags, output) : -ENOSYS;
    }
    if (!output || !size || size > LLONG_MAX) { errno = EINVAL; return -EINVAL; }
    if (fcntl(fd, F_GETFD) < 0) return -errno;
    int buffer = syscall(__NR_memfd_create, "a10vm-codec-linear", closeOnExec);
    if (buffer < 0) return -errno;
    if (syscall(__NR_ftruncate, buffer, size) < 0) {
        int error = errno;
        close(buffer);
        errno = error;
        return -error;
    }
    *output = buffer;
    return 0;
}
