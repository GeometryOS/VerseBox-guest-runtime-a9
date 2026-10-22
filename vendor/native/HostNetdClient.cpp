#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <atomic>
#include "HostConnect.h"

namespace {
std::atomic<unsigned> processNetwork{0}, resolverNetwork{0};

unsigned networkId(const char* name) {
    const char* state = getenv("A10VM_STATE_ROOT");
    if (!state) return 0;
    char path[PATH_MAX], value[32]{};
    if (snprintf(path, sizeof(path), "%s/%s", state, name) >= static_cast<int>(sizeof(path))) return 0;
    int fd = syscall(__NR_openat, AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return 0;
    ssize_t size = syscall(__NR_read, fd, value, sizeof(value) - 1);
    syscall(__NR_close, fd);
    return size > 0 ? strtoul(value, nullptr, 10) : 0;
}

unsigned hostNetwork(unsigned guest) {
    if (!(guest & 0x7fffffffU)) return guest;
    return networkId("host-network-id") | (guest & 0x80000000U);
}

uid_t hostUid(uid_t guest) { return guest == static_cast<uid_t>(-1) ? guest : syscall(__NR_getuid); }

template<typename Function> Function original(const char* name) {
    static void* library = dlopen("liba10vm-host-netd-client.so", RTLD_NOW | RTLD_LOCAL);
    return library ? reinterpret_cast<Function>(dlsym(library, name)) : nullptr;
}

using Accept = int (*)(int, sockaddr*, socklen_t*, int);
using Connect = int (*)(int, const sockaddr*, socklen_t);
using Socket = int (*)(int, int, int);
using Resolver = unsigned (*)(unsigned);
using DnsProxy = int (*)();
Resolver resolveNetwork;

unsigned mappedResolver(unsigned netId) {
    unsigned selected = netId ? netId : resolverNetwork.load();
    return resolveNetwork ? resolveNetwork(hostNetwork(selected)) : hostNetwork(selected);
}

template<typename Function> void initialize(const char* name, Function* dispatch) {
    auto init = original<void (*)(Function*)>(name);
    if (init) init(dispatch);
}
}

extern "C" void netdClientInitAccept4(Accept* function) { initialize("netdClientInitAccept4", function); }
extern "C" void netdClientInitConnect(Connect* function) {
    if (!function) return;
    Connect raw = *function;
    initialize("netdClientInitConnect", function);
    initializeHostConnect(raw, *function);
    *function = hostConnect;
}
extern "C" void netdClientInitSocket(Socket* function) { initialize("netdClientInitSocket", function); }
extern "C" void netdClientInitDnsOpenProxy(DnsProxy* function) { initialize("netdClientInitDnsOpenProxy", function); }
extern "C" void netdClientInitNetIdForResolv(Resolver* function) {
    if (!function) return;
    initialize("netdClientInitNetIdForResolv", function);
    resolveNetwork = *function;
    *function = mappedResolver;
}

#define FORWARD_INT(name, signature, arguments) \
    extern "C" int name signature { \
        auto function = original<int (*) signature>(#name); \
        return function ? function arguments : -ENOSYS; \
    }

FORWARD_INT(setNetworkForSocket, (unsigned netId, int fd), (hostNetwork(netId), fd))
FORWARD_INT(protectFromVpn, (int fd), (fd))
FORWARD_INT(setNetworkForUser, (uid_t uid, int fd), (hostUid(uid), fd))
FORWARD_INT(queryUserAccess, (uid_t uid, unsigned netId), (hostUid(uid), hostNetwork(netId)))
FORWARD_INT(tagSocket, (int fd, uint32_t tag, uid_t uid), (fd, tag, hostUid(uid)))
extern "C" int tagSocketWithPid(int fd, uint32_t tag, uid_t uid, pid_t pid) {
    auto function = original<int (*)(int, uint32_t, uid_t, pid_t)>("tagSocketWithPid");
    return function ? function(fd, tag, hostUid(uid), pid) : tagSocket(fd, tag, uid);
}
FORWARD_INT(untagSocket, (int fd), (fd))
FORWARD_INT(setCounterSet, (uint32_t counter, uid_t uid), (counter, hostUid(uid)))
FORWARD_INT(deleteTagData, (uint32_t tag, uid_t uid), (tag, hostUid(uid)))
FORWARD_INT(resNetworkQuery, (unsigned netId, const char* name, int klass, int type, uint32_t flags),
            (hostNetwork(netId), name, klass, type, flags))
FORWARD_INT(resNetworkSend, (unsigned netId, const uint8_t* message, size_t length, uint32_t flags),
            (hostNetwork(netId), message, length, flags))
FORWARD_INT(resNetworkResult, (int fd, int* rcode, uint8_t* answer, size_t length), (fd, rcode, answer, length))

extern "C" void resNetworkCancel(int fd) {
    auto function = original<void (*)(int)>("resNetworkCancel");
    if (function) function(fd);
}

extern "C" unsigned getNetworkForProcess() { return processNetwork.load(); }
extern "C" int setNetworkForProcess(unsigned netId) {
    auto function = original<int (*)(unsigned)>("setNetworkForProcess");
    int result = function ? function(hostNetwork(netId)) : -ENOSYS;
    if (!result) processNetwork.store(netId);
    return result;
}
extern "C" int setNetworkForResolv(unsigned netId) {
    auto function = original<int (*)(unsigned)>("setNetworkForResolv");
    int result = function ? function(hostNetwork(netId)) : -ENOSYS;
    if (!result) resolverNetwork.store(netId);
    return result;
}

extern "C" int getNetworkForSocket(unsigned* netId, int fd) {
    auto function = original<int (*)(unsigned*, int)>("getNetworkForSocket");
    int result = function ? function(netId, fd) : -ENOSYS;
    if (!result && netId && *netId == networkId("host-network-id")) *netId = networkId("guest-network-id");
    return result;
}
extern "C" int getNetworkForDns(unsigned* netId) {
    auto function = original<int (*)(unsigned*)>("getNetworkForDns");
    int result = function ? function(netId) : -ENOSYS;
    if (!result && netId && *netId == networkId("host-network-id")) *netId = networkId("guest-network-id");
    return result;
}
