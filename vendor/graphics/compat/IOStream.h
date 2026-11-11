#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

// The generated codec needs buffered I/O, not the desktop VM snapshot API.
class IOStream {
public:
    // A 4096-square RGBA upload also carries codec fields and a checksum.
    static constexpr size_t maxTransfer = 64 * 1024 * 1024 + 64 * 1024;
    explicit IOStream(size_t capacity = 64 * 1024) : buffer_(capacity) {}
    virtual ~IOStream() = default;
    IOStream(const IOStream&) = delete;
    IOStream& operator=(const IOStream&) = delete;

    unsigned char* alloc(size_t size) {
        if (size > maxTransfer) throw std::length_error("GL allocation exceeds probe limit");
        if (size > buffer_.size() - used_) flush();
        if (size > buffer_.size()) buffer_.resize(size);
        auto* result = buffer_.data() + used_;
        used_ += size;
        return result;
    }

    int flush() {
        if (!used_) return 0;
        if (writeFully(buffer_.data(), used_) < 0) {
            throw std::runtime_error("GL stream flush failed");
        }
        used_ = 0;
        return 0;
    }

    const unsigned char* readback(void* data, size_t size) {
        flush();
        return readFully(data, size);
    }

    virtual int writeFully(const void* data, size_t size) = 0;
    virtual const unsigned char* readFully(void* data, size_t size) = 0;
    void readbackPixels(void* context, int width, int height, unsigned int format,
                        unsigned int type, void* pixels);

    void* getDmaForReading(uint64_t) {
        throw std::runtime_error("Goldfish DMA is not part of the APK transport");
    }
    void unlockDma(uint64_t) {
        throw std::runtime_error("Goldfish DMA is not part of the APK transport");
    }

private:
    std::vector<unsigned char> buffer_;
    size_t used_ = 0;
};
