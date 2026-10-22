#include <cutils/uevent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {
std::mutex mutex;
std::vector<int> writers;
std::mutex legacyMutex;
int legacySocket = -1;
std::set<std::string> wakeLocks;
}

extern "C" int uevent_open_socket(int bytes, bool credentials) {
    if (!getenv("A10VM_ROOTFS")) {
        auto original = reinterpret_cast<decltype(&uevent_open_socket)>(dlsym(RTLD_NEXT, "uevent_open_socket"));
        if (!original) { errno = ENOSYS; return -1; }
        return original(bytes, credentials);
    }
    // No physical devices are exposed: observers listen on a private, empty device bus.
    int channels[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, channels) < 0) return -1;
    setsockopt(channels[0], SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));
    std::lock_guard<std::mutex> lock(mutex);
    writers.push_back(channels[1]);
    return channels[0];
}

extern "C" ssize_t uevent_kernel_recv(int fd, void* buffer, size_t length, bool grouped, uid_t* uid) {
    if (!getenv("A10VM_ROOTFS")) {
        auto original = reinterpret_cast<decltype(&uevent_kernel_recv)>(dlsym(RTLD_NEXT, "uevent_kernel_recv"));
        if (!original) { errno = ENOSYS; return -1; }
        return original(fd, buffer, length, grouped, uid);
    }
    if (uid) *uid = getuid();
    return recv(fd, buffer, length, 0);
}

extern "C" ssize_t uevent_kernel_multicast_uid_recv(int fd, void* buffer, size_t length, uid_t* uid) {
    return uevent_kernel_recv(fd, buffer, length, true, uid);
}

extern "C" ssize_t uevent_kernel_multicast_recv(int fd, void* buffer, size_t length) {
    uid_t uid;
    return uevent_kernel_recv(fd, buffer, length, true, &uid);
}

extern "C" int uevent_init() {
    if (!getenv("A10VM_ROOTFS")) {
        auto original = reinterpret_cast<int (*)()>(dlsym(RTLD_NEXT, "uevent_init"));
        return original ? original() : 0;
    }
    std::lock_guard<std::mutex> lock(legacyMutex);
    if (legacySocket < 0) legacySocket = uevent_open_socket(64 * 1024, false);
    return legacySocket >= 0;
}

extern "C" int uevent_get_fd() {
    if (getenv("A10VM_ROOTFS")) return legacySocket;
    auto original = reinterpret_cast<int (*)()>(dlsym(RTLD_NEXT, "uevent_get_fd"));
    return original ? original() : -1;
}

extern "C" int uevent_next_event(char* buffer, int length) {
    if (!getenv("A10VM_ROOTFS")) {
        auto original = reinterpret_cast<int (*)(char*, int)>(dlsym(RTLD_NEXT, "uevent_next_event"));
        return original ? original(buffer, length) : -1;
    }
    ssize_t result;
    do { result = recv(legacySocket, buffer, length, 0); } while (result < 0 && errno == EINTR);
    return result;
}

extern "C" int acquire_wake_lock(int type, const char* name) {
    if (!getenv("A10VM_ROOTFS")) {
        auto original = reinterpret_cast<int (*)(int, const char*)>(dlsym(RTLD_NEXT, "acquire_wake_lock"));
        return original ? original(type, name) : -1;
    }
    if (!name) { errno = EINVAL; return -1; }
    std::lock_guard<std::mutex> lock(mutex);
    wakeLocks.insert(name);
    return 0;
}

extern "C" int release_wake_lock(const char* name) {
    if (!getenv("A10VM_ROOTFS")) {
        auto original = reinterpret_cast<int (*)(const char*)>(dlsym(RTLD_NEXT, "release_wake_lock"));
        return original ? original(name) : -1;
    }
    if (!name) { errno = EINVAL; return -1; }
    std::lock_guard<std::mutex> lock(mutex);
    wakeLocks.erase(name);
    return 0;
}
