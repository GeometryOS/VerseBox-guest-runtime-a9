#include "Driver.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/android/binder.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

namespace {
// pthread key instead of thread_local: A9's linker has no R_AARCH64_TLSDESC
// handler and its R_AARCH64_TLS_TPREL64 branch writes nothing, so a
// shared-library TLS variable would jump to 0 or corrupt the thread pointer.
pthread_key_t insideDriverKey;
pthread_once_t insideDriverOnce = PTHREAD_ONCE_INIT;
void createInsideDriverKey() { pthread_key_create(&insideDriverKey, nullptr); }
bool insideDriver() {
    pthread_once(&insideDriverOnce, createInsideDriverKey);
    return pthread_getspecific(insideDriverKey) != nullptr;
}
void setInsideDriver(bool value) {
    pthread_once(&insideDriverOnce, createInsideDriverKey);
    pthread_setspecific(insideDriverKey, value ? reinterpret_cast<void*>(1) : nullptr);
}

// Keep a one-line record of the fault address. Guest processes have no
// debuggerd handler, so without this a SIGSEGV leaves nothing in the log.
static void crashHandler(int sig, siginfo_t* info, void* context) {
    ucontext_t* uc = static_cast<ucontext_t*>(context);
    char buffer[320];
    int length = snprintf(buffer, sizeof(buffer),
                          "A9VM bdev crash sig=%d pc=%llx lr=%llx sp=%llx addr=%llx\n",
                          sig, static_cast<unsigned long long>(uc->uc_mcontext.pc),
                          static_cast<unsigned long long>(uc->uc_mcontext.regs[30]),
                          static_cast<unsigned long long>(uc->uc_mcontext.sp),
                          static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info->si_addr)));
    if (length > 0) write(2, buffer, static_cast<size_t>(length));
    _exit(128 + sig);
}

static void trace(const char* text) { write(2, text, strlen(text)); }

// libc runs its own constructors (stdio, locale, isatty...) before this shim's
// constructors, and those call close/ioctl/mmap. liba9binder's globals (the
// driver map) are not constructed yet at that point, so every override must
// pass through until this shim is fully initialized. Without this guard the
// first libc-constructor hit lands in a not-yet-constructed std::map and
// segfaults before the program's main() is reached.
bool ready = false;
struct Activator {
    Activator() {
        ready = true;
        struct sigaction action {};
        action.sa_sigaction = crashHandler;
        action.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &action, nullptr);
        sigaction(SIGBUS, &action, nullptr);
        sigaction(SIGILL, &action, nullptr);
    }
} activator;

struct DriverCall {
    DriverCall() { setInsideDriver(true); }
    ~DriverCall() { setInsideDriver(false); }
};

const char* device(const char* path) {
    if (!path || insideDriver()) return nullptr;
    if (!strcmp(path, "/dev/binder")) return a10binder_default_driver();
    if (!strcmp(path, "/dev/hwbinder")) return a10binder_default_hwbinder_driver();
    return nullptr;
}

int openDevice(const char* path, int flags, mode_t mode) {
    const char* endpoint = device(path);
    if (!endpoint) return openat(AT_FDCWD, path, flags, mode);
    if ((flags & O_ACCMODE) != O_RDWR) { errno = EINVAL; return -1; }
    DriverCall call;
    return a10binder_open(endpoint);
}
}

extern "C" int open(const char* path, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    if (!ready) return static_cast<int>(syscall(__NR_openat, AT_FDCWD, path, flags, mode));
    return openDevice(path, flags, mode);
}

extern "C" int __open_2(const char* path, int flags) {
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) { errno = EINVAL; return -1; }
    if (!ready) return static_cast<int>(syscall(__NR_openat, AT_FDCWD, path, flags, 0));
    return openDevice(path, flags, 0);
}

extern "C" int access(const char* path, int mode) {
    if (!ready) return static_cast<int>(syscall(__NR_faccessat, AT_FDCWD, path, mode));
    const char* endpoint = device(path);
    return faccessat(AT_FDCWD, endpoint ? endpoint : path, mode, 0);
}

extern "C" int ioctl(int fd, int request, ...) {
    va_list args;
    va_start(args, request);
    void* argument = va_arg(args, void*);
    va_end(args);
    if (!ready) return static_cast<int>(syscall(__NR_ioctl, fd, static_cast<unsigned int>(request), argument));
    if (!insideDriver() && a10binder_is_driver(fd)) {
        DriverCall call;
        int result = a10binder_ioctl(fd, static_cast<unsigned int>(request), argument);
        static int traceCount = 0;
        if (static_cast<unsigned int>(request) == BINDER_WRITE_READ && traceCount < 24) {
            char line[128];
            int length = snprintf(line, sizeof(line), "bdev writeRead fd=%d ret=%d errno=%d\n", fd, result, errno);
            if (length > 0) write(2, line, static_cast<size_t>(length));
            ++traceCount;
        }
        if (result == 0 && static_cast<unsigned int>(request) == BINDER_SET_CONTEXT_MGR) {
            printf("binder_context_pid=%d\nbinder_context_ready=1\n", getpid());
            fflush(stdout);
        }
        return result;
    }
    return static_cast<int>(syscall(__NR_ioctl, fd, static_cast<unsigned int>(request), argument));
}

extern "C" int close(int fd) {
    if (!ready) return static_cast<int>(syscall(__NR_close, fd));
    if (!insideDriver() && a10binder_is_driver(fd)) {
        DriverCall call;
        return a10binder_close(fd);
    }
    return static_cast<int>(syscall(__NR_close, fd));
}

extern "C" void* mmap(void* address, size_t size, int protection, int flags, int fd, off_t offset) {
    if (!ready)
        return reinterpret_cast<void*>(syscall(__NR_mmap, address, size, protection, flags, fd, offset));
    // Anonymous allocator maps must never acquire the Binder metadata lock.
    if (fd >= 0 && !(flags & MAP_ANONYMOUS) && !insideDriver() && a10binder_is_driver(fd)) {
        if (offset || (protection & PROT_WRITE)) { errno = EINVAL; return MAP_FAILED; }
        protection = PROT_NONE;
        flags = (flags & ~MAP_SHARED) | MAP_PRIVATE | MAP_ANONYMOUS;
        fd = -1;
    }
    return reinterpret_cast<void*>(syscall(__NR_mmap, address, size, protection, flags, fd, offset));
}

extern "C" void* mmap64(void* address, size_t size, int protection, int flags, int fd, off64_t offset) {
    return mmap(address, size, protection, flags, fd, offset);
}
