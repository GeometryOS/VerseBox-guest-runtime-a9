#include "SocketStream.h"

#include <cerrno>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <system_error>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
[[noreturn]] void fail(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

int localSocket(const std::string& path, bool server) {
    sockaddr_un address{};
    if (path.size() >= sizeof(address.sun_path)) throw std::length_error("Socket path too long");
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) fail("socket");
    const auto length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
    const int result = server ? bind(fd, reinterpret_cast<sockaddr*>(&address), length)
                              : connect(fd, reinterpret_cast<sockaddr*>(&address), length);
    if (result < 0 || (server && listen(fd, 1) < 0)) {
        const int error = errno;
        close(fd);
        errno = error;
        fail(server ? "listen" : "connect");
    }
    return fd;
}
}

int connectLocal(const std::string& path) { return localSocket(path, false); }
int listenLocal(const std::string& path) { return localSocket(path, true); }

SocketStream::SocketStream(int fd, size_t capacity, int timeoutMs, bool bufferedReads)
    : IOStream(capacity), fd_(fd), received_(bufferedReads ? capacity : 0) {
    const timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (fd < 0) throw std::invalid_argument("Invalid GL socket");
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        const int error = errno;
        close(fd);
        errno = error;
        fail("socket timeout");
    }
}

SocketStream::~SocketStream() { close(fd_); }

int SocketStream::writeFully(const void* data, size_t size) {
    const auto* cursor = static_cast<const unsigned char*>(data);
    while (size) {
        const auto count = send(fd_, cursor, size, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) fail("GL send");
        if (!count) throw std::runtime_error("GL send made no progress");
        cursor += count;
        size -= count;
    }
    return 0;
}

bool SocketStream::readOrEof(void* data, size_t size) {
    auto* cursor = static_cast<unsigned char*>(data);
    size_t remaining = size;
    while (remaining) {
        if (readOffset_ < readSize_) {
            const auto count = std::min(remaining, readSize_ - readOffset_);
            std::memcpy(cursor, received_.data() + readOffset_, count);
            readOffset_ += count;
            cursor += count;
            remaining -= count;
            continue;
        }
        const bool buffer = !received_.empty() && remaining < received_.size();
        const auto count = recv(fd_, buffer ? received_.data() : cursor,
                                buffer ? received_.size() : remaining, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) fail("GL receive");
        if (!count) {
            if (remaining == size) return false;
            throw std::runtime_error("Truncated GL stream");
        }
        if (buffer) {
            readOffset_ = 0;
            readSize_ = count;
            continue;
        }
        cursor += count;
        remaining -= count;
    }
    return true;
}

const unsigned char* SocketStream::readFully(void* data, size_t size) {
    if (!readOrEof(data, size)) throw std::runtime_error("Unexpected GL EOF");
    return static_cast<unsigned char*>(data);
}
