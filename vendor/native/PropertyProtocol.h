#pragma once
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

enum { PROPERTY_SET = 0x00020001, PROPERTY_NAME_LIMIT = 128, PROPERTY_VALUE_LIMIT = 92 };

static inline int property_read(int fd, void* output, size_t size) {
    if (!size) return 1;
    ssize_t count;
    do { count = recv(fd, output, size, MSG_WAITALL); } while (count < 0 && errno == EINTR);
    return count == (ssize_t)size;
}

static inline int property_write(int fd, const void* input, size_t size) {
    const char* bytes = (const char*)input;
    while (size) {
        ssize_t count = send(fd, bytes, size, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return 0;
        bytes += count;
        size -= count;
    }
    return 1;
}
