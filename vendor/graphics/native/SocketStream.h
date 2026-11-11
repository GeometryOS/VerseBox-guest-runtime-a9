#pragma once

#include "IOStream.h"
#include <string>
#include <vector>

class SocketStream final : public IOStream {
public:
    explicit SocketStream(int fd, size_t capacity = 64 * 1024, int timeoutMs = 5000, bool bufferedReads = false);
    ~SocketStream() override;
    int writeFully(const void* data, size_t size) override;
    const unsigned char* readFully(void* data, size_t size) override;
    bool readOrEof(void* data, size_t size);
    int fd() const { return fd_; }

private:
    int fd_;
    std::vector<unsigned char> received_;
    size_t readOffset_ = 0, readSize_ = 0;
};

int connectLocal(const std::string& path);
int listenLocal(const std::string& path);
