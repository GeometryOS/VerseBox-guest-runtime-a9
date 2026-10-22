#include "HostDeviceTransport.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

HostDeviceSocket::HostDeviceSocket(const char* name, uint32_t magic, uint32_t command, bool polling) {
    const char* root = getenv("A10VM_STATE_ROOT");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (!root || snprintf(address.sun_path, sizeof(address.sun_path), "%s/%s.sock", root, name) >=
            static_cast<int>(sizeof(address.sun_path))) return;
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    timeval timeout{polling ? 0 : 3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    timeout.tv_sec = 3;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
            !put(magic) || !put(1) || !put(command)) { close(fd); fd = -1; }
}
HostDeviceSocket::~HostDeviceSocket() { if (fd >= 0) close(fd); }
void HostDeviceSocket::cancel() { if (fd >= 0) shutdown(fd, SHUT_RDWR); }
bool HostDeviceSocket::transfer(void* data, size_t size, bool writing) {
    auto* bytes = static_cast<char*>(data);
    while (size) {
        ssize_t count = writing ? send(fd, bytes, size, MSG_NOSIGNAL) : recv(fd, bytes, size, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        bytes += count; size -= count;
    }
    return true;
}
bool HostDeviceSocket::put(uint32_t value) { value = htonl(value); return transfer(&value, 4, true); }
bool HostDeviceSocket::put64(uint64_t value) { return put(value >> 32) && put(value); }
bool HostDeviceSocket::get(int32_t* value) {
    uint32_t bytes;
    if (!transfer(&bytes, 4, false)) return false;
    *value = ntohl(bytes); return true;
}
bool HostDeviceSocket::get64(int64_t* value) {
    int32_t high, low;
    if (!get(&high) || !get(&low)) return false;
    *value = (static_cast<uint64_t>(static_cast<uint32_t>(high)) << 32) | static_cast<uint32_t>(low);
    return true;
}
bool HostDeviceSocket::getFloat(float* value) {
    int32_t bits;
    if (!get(&bits)) return false;
    memcpy(value, &bits, 4); return true;
}
bool HostDeviceSocket::getString(std::string* value, int32_t limit) {
    int32_t size;
    if (!get(&size) || size < 0 || size > limit) return false;
    value->resize(size); return transfer(value->data(), size, false);
}

int HostDeviceSocket::getDescriptor() {
    int32_t result = 0;
    int descriptor = -1;
    size_t received = 0;
    while (received < sizeof(result)) {
        iovec data{reinterpret_cast<char*>(&result) + received, sizeof(result) - received};
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * 4)]{};
        msghdr message{};
        message.msg_iov = &data;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        ssize_t count;
        do { count = recvmsg(fd, &message, MSG_CMSG_CLOEXEC); } while (count < 0 && errno == EINTR);
        int error = count < 0 ? errno : EPROTO;
        for (cmsghdr* item = CMSG_FIRSTHDR(&message); item; item = CMSG_NXTHDR(&message, item)) {
            if (item->cmsg_level != SOL_SOCKET || item->cmsg_type != SCM_RIGHTS) continue;
            size_t bytes = item->cmsg_len - CMSG_LEN(0);
            auto* descriptors = reinterpret_cast<int*>(CMSG_DATA(item));
            for (size_t i = 0; i < bytes / sizeof(int); ++i) {
                if (descriptor < 0) descriptor = descriptors[i];
                else close(descriptors[i]);
            }
        }
        if (count <= 0 || (message.msg_flags & (MSG_CTRUNC | MSG_TRUNC))) {
            if (descriptor >= 0) close(descriptor);
            errno = error;
            return -1;
        }
        // Stream reads may stop after the byte carrying SCM_RIGHTS.
        received += count;
    }
    if (ntohl(result) == 0 && descriptor >= 0) return descriptor;
    if (descriptor >= 0) close(descriptor);
    errno = static_cast<int32_t>(ntohl(result)) < 0 ? -static_cast<int32_t>(ntohl(result)) : EPROTO;
    return -1;
}
