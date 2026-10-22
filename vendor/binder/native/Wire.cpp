#include "Wire.h"
#include <array>
#include <errno.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

namespace a10binder {
// Per-thread 64 KiB receive scratch. pthread key instead of thread_local
// because A9's linker cannot relocate shared-library TLS (see Driver.cpp).
static pthread_key_t receiveBufferKey;
static pthread_once_t receiveBufferOnce = PTHREAD_ONCE_INIT;
static void createReceiveBufferKey() { pthread_key_create(&receiveBufferKey, nullptr); }
static uint8_t* receiveBuffer() {
    pthread_once(&receiveBufferOnce, createReceiveBufferKey);
    uint8_t* buffer = static_cast<uint8_t*>(pthread_getspecific(receiveBufferKey));
    if (!buffer) {
        buffer = static_cast<uint8_t*>(malloc(kInlinePacket));
        if (buffer) pthread_setspecific(receiveBufferKey, buffer);
    }
    return buffer;
}

Packet::~Packet() { for (int fd : fds) close(fd); }
Packet::Packet(Packet&& other) noexcept : header(other.header), data(std::move(other.data)) {
    fds.swap(other.fds);
}
Packet& Packet::operator=(Packet&& other) noexcept {
    if (this != &other) {
        for (int fd : fds) close(fd);
        fds.clear();
        header = other.header;
        data = std::move(other.data);
        fds.swap(other.fds);
    }
    return *this;
}
size_t aligned(size_t value) { return (value + 7) & ~size_t(7); }

int connectSocket(const char* path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (!path || path[0] != '/' || strlen(path) >= sizeof(address.sun_path) ||
        strncmp(path, "/dev/", 5) == 0) { errno = EINVAL; return -1; }
    strcpy(address.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) return fd;
    int error = errno;
    close(fd);
    errno = error;
    return -1;
}

bool sendPacket(int fd, const Packet& packet, int flags) {
    if (packet.data.size() + sizeof(Header) > kMaxPacket || packet.fds.size() > kMaxFds) {
        errno = EMSGSIZE;
        return false;
    }
    Header header = packet.header;
    uint64_t size = packet.data.size();
    int shared = -1;
    std::vector<int> descriptors = packet.fds;
    iovec io[] = {{&header, sizeof(Header)},
                  {const_cast<uint8_t*>(packet.data.data()), packet.data.size()}};
    if (size + sizeof(Header) > kInlinePacket) {
        shared = syscall(__NR_memfd_create, "a10vm-binder-parcel", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (shared < 0) return false;
        size_t written = 0;
        while (written < size) {
            ssize_t count = TEMP_FAILURE_RETRY(write(shared, packet.data.data() + written, size - written));
            if (count <= 0) { int error = errno; close(shared); errno = error; return false; }
            written += count;
        }
        if (fcntl(shared, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) < 0) {
            int error = errno; close(shared); errno = error; return false;
        }
        header.reserved = kSharedPayload;
        descriptors.push_back(shared);
        io[1] = {&size, sizeof(size)};
    }
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * (kMaxFds + 1))]{};
    msghdr message{};
    message.msg_iov = io;
    message.msg_iovlen = 2;
    if (!descriptors.empty()) {
        message.msg_control = control;
        message.msg_controllen = CMSG_SPACE(sizeof(int) * descriptors.size());
        cmsghdr* cmsg = CMSG_FIRSTHDR(&message);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * descriptors.size());
        memcpy(CMSG_DATA(cmsg), descriptors.data(), sizeof(int) * descriptors.size());
    }
    ssize_t count;
    do { count = sendmsg(fd, &message, flags | MSG_NOSIGNAL); } while (count < 0 && errno == EINTR);
    int error = errno;
    if (shared >= 0) close(shared);
    errno = error;
    return count == static_cast<ssize_t>(sizeof(Header) + io[1].iov_len);
}

bool receivePacket(int fd, Packet* packet, int flags) {
    Packet received;
    // recvmsg supplies the valid byte count; retaining capacity avoids a 64 KiB
    // allocation, zero-fill and Scudo release on every small Binder command.
    uint8_t* bytes = receiveBuffer();
    if (!bytes) { errno = ENOMEM; return false; }
    iovec io{bytes, kInlinePacket};
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * (kMaxFds + 1))]{};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t count;
    do { count = recvmsg(fd, &message, flags | MSG_CMSG_CLOEXEC); } while (count < 0 && errno == EINTR);
    if (count <= 0) { if (count == 0) errno = ECONNRESET; return false; }
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&message); cmsg; cmsg = CMSG_NXTHDR(&message, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
            cmsg->cmsg_len < CMSG_LEN(0)) { errno = EPROTO; return false; }
        size_t length = cmsg->cmsg_len - CMSG_LEN(0);
        if (length % sizeof(int)) { errno = EPROTO; return false; }
        const int* begin = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
        received.fds.insert(received.fds.end(), begin, begin + length / sizeof(int));
    }
    if ((message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) || count < static_cast<ssize_t>(sizeof(Header))) {
        errno = EMSGSIZE;
        return false;
    }
    memcpy(&received.header, bytes, sizeof(Header));
    if (received.header.magic != kMagic || received.header.version != kVersion ||
        received.header.reserved > kSharedPayload) {
        errno = EPROTO;
        return false;
    }
    if (received.header.reserved == kSharedPayload) {
        uint64_t size;
        struct stat info{};
        if (count != sizeof(Header) + sizeof(size) || received.fds.empty()) { errno = EPROTO; return false; }
        memcpy(&size, bytes + sizeof(Header), sizeof(size));
        int shared = received.fds.back();
        int seals = fcntl(shared, F_GET_SEALS);
        constexpr int required = F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK;
        if (size > kMaxPacket - sizeof(Header) || fstat(shared, &info) < 0 ||
            info.st_size < 0 || static_cast<uint64_t>(info.st_size) != size ||
            seals < 0 || (seals & required) != required) { errno = EPROTO; return false; }
        received.data.resize(size);
        size_t read = 0;
        while (read < size) {
            ssize_t length = TEMP_FAILURE_RETRY(pread(shared, received.data.data() + read, size - read, read));
            if (length <= 0) { if (!length) errno = EPROTO; return false; }
            read += length;
        }
        close(shared);
        received.fds.pop_back();
        received.header.reserved = 0;
    } else {
        received.data.assign(bytes + sizeof(Header), bytes + count);
    }
    if (received.fds.size() > kMaxFds) { errno = EMSGSIZE; return false; }
    *packet = std::move(received);
    return true;
}

int inspect(const char* path, Stats* stats) {
    int fd = connectSocket(path);
    if (fd < 0) return -1;
    Packet request, reply;
    request.header.kind = Inspect;
    bool ok = sendPacket(fd, request) && receivePacket(fd, &reply) && reply.header.kind == Ack &&
              reply.header.status == 0 && reply.data.size() == sizeof(*stats) && reply.get(0, stats);
    int error = errno;
    close(fd);
    errno = error;
    return ok ? 0 : -1;
}
}
