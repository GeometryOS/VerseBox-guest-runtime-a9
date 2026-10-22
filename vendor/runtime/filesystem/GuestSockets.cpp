#include "GuestPaths.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
int call(long operation, int fd, const sockaddr* address, socklen_t length) {
    constexpr size_t prefix = offsetof(sockaddr_un, sun_path);
    if (!guestRoot() || !address || length <= prefix || address->sa_family != AF_UNIX)
        return syscall(operation, fd, address, length);
    auto unixAddress = reinterpret_cast<const sockaddr_un*>(address);
    if (!unixAddress->sun_path[0]) return syscall(operation, fd, address, length);
    size_t size = length - prefix;
    if (size > sizeof(unixAddress->sun_path)) size = sizeof(unixAddress->sun_path);
    char logical[sizeof(unixAddress->sun_path) + 1]{};
    memcpy(logical, unixAddress->sun_path, size);
    if (operation == __NR_connect && getenv("A10VM_HOST_NETWORK") &&
            !strcmp(getenv("A10VM_HOST_NETWORK"), "1") &&
            (!strcmp(logical, "/dev/socket/dnsproxyd") || !strcmp(logical, "/dev/socket/fwmarkd")))
        return syscall(operation, fd, address, length);
    char buffer[PATH_MAX];
    const char* mapped = guestPath(AT_FDCWD, logical, buffer);
    if (!mapped) return -1;
    if (mapped == logical) return syscall(operation, fd, address, length);
    sockaddr_un target{};
    target.sun_family = AF_UNIX;
    size_t bytes = strlen(mapped);
    if (bytes >= sizeof(target.sun_path)) { errno = ENAMETOOLONG; return -1; }
    memcpy(target.sun_path, mapped, bytes + 1);
    return syscall(operation, fd, &target, prefix + bytes + 1);
}
}

int bind(int fd, const sockaddr* address, socklen_t length) { return call(__NR_bind, fd, address, length); }
extern "C" int __connect(int fd, const sockaddr* address, socklen_t length) { return call(__NR_connect, fd, address, length); }
