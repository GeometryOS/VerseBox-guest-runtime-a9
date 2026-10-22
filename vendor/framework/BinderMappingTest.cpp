#include "Driver.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

static pthread_mutex_t driverLock = PTHREAD_MUTEX_INITIALIZER;
static unsigned lookups;
static constexpr int binderFd = 42;
alignas(64) static thread_local bool testThread;

extern "C" int a10binder_is_driver(int fd) {
    pthread_mutex_lock(&driverLock);
    ++lookups;
    pthread_mutex_unlock(&driverLock);
    return fd == binderFd;
}
extern "C" const char* a10binder_default_driver() { return "/unused"; }
extern "C" const char* a10binder_default_hwbinder_driver() { return "/unused"; }
extern "C" int a10binder_open(const char*) { return -1; }
extern "C" int a10binder_close(int) { return -1; }
extern "C" int a10binder_ioctl(int, unsigned long, void*) { return -1; }

int main() {
    testThread = true;
    // Allocators map anonymous pages while Binder metadata can be locked elsewhere.
    alarm(2);
    pthread_mutex_lock(&driverLock);
    lookups = 0;
    const int descriptors[] = {-1, binderFd};
    for (int fd : descriptors) {
        void* page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);
        assert(page != MAP_FAILED);
        *static_cast<char*>(page) = 1;
        assert(munmap(page, 4096) == 0);
        page = mmap64(nullptr, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);
        assert(page != MAP_FAILED);
        assert(munmap(page, 4096) == 0);
    }
    assert(lookups == 0);
    pthread_mutex_unlock(&driverLock);
    alarm(0);
    void* binder = mmap(nullptr, 4096, PROT_READ, MAP_PRIVATE, binderFd, 0);
    assert(binder != MAP_FAILED && lookups == 1);
    assert(munmap(binder, 4096) == 0);
    assert(mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, binderFd, 0) == MAP_FAILED);
    assert(errno == EINVAL);
    assert(mmap(nullptr, 4096, PROT_READ, MAP_PRIVATE, binderFd, 4096) == MAP_FAILED);
    assert(errno == EINVAL);
    puts("binder_anonymous_mapping_passed=1");
}
