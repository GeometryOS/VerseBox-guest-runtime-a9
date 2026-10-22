#include "HostConnect.h"
#include <FwmarkCommand.h>
#include <atomic>
#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
HostConnectFunction rawConnect, legacyConnect;
std::atomic<bool> extendedConnect{false};

int markConnect(int fd, const sockaddr* address) {
    static_assert(sizeof(FwmarkCommand) == 16 && sizeof(FwmarkConnectInfo) == 36, "Fwmark wire ABI");
    int channel = syscall(__NR_socket, AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (channel < 0) return -errno;
    sockaddr_un path{AF_UNIX, "/dev/socket/fwmarkd"};
    int result = 0;
    if (TEMP_FAILURE_RETRY(syscall(__NR_connect, channel, &path, sizeof(path))) < 0) {
        result = -errno;
    } else {
        FwmarkCommand command{FwmarkCommand::ON_CONNECT, 0, 0, 0};
        FwmarkConnectInfo info;
        memset(&info, 0, sizeof(info));
        memcpy(&info.addr, address, address->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
        iovec vectors[]{{&command, sizeof(command)}, {&info, sizeof(info)}};
        union { cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(fd))]; } control{};
        msghdr message{};
        message.msg_iov = vectors;
        message.msg_iovlen = 2;
        message.msg_control = control.bytes;
        message.msg_controllen = sizeof(control.bytes);
        cmsghdr* header = CMSG_FIRSTHDR(&message);
        header->cmsg_len = CMSG_LEN(sizeof(fd));
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(header), &fd, sizeof(fd));
        ssize_t sent = TEMP_FAILURE_RETRY(sendmsg(channel, &message, MSG_NOSIGNAL));
        if (sent < 0) result = -errno;
        else if (sent != sizeof(command) + sizeof(info)) result = -EBADMSG;
        else {
            ssize_t received = TEMP_FAILURE_RETRY(recv(channel, &result, sizeof(result), MSG_WAITALL));
            if (received < 0) result = -errno;
            else if (received != sizeof(result)) result = -EBADMSG;
        }
    }
    syscall(__NR_close, channel);
    return result;
}
}

void initializeHostConnect(HostConnectFunction raw, HostConnectFunction legacy) {
    rawConnect = raw;
    legacyConnect = legacy;
}

int hostConnect(int fd, const sockaddr* address, socklen_t length) {
    bool inet = address && ((address->sa_family == AF_INET && length >= sizeof(sockaddr_in)) ||
            (address->sa_family == AF_INET6 && length >= sizeof(sockaddr_in6)));
    if (!inet || !extendedConnect.load()) {
        int result = legacyConnect(fd, address, length);
        // Modern netd rejects the old ON_CONNECT before the TCP syscall is attempted.
        if (result != -1 || errno != EBADMSG || !inet) return result;
        extendedConnect.store(true);
    }
    int result = markConnect(fd, address);
    if (result) { errno = -result; return -1; }
    return rawConnect(fd, address, length);
}
