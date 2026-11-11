#pragma once

#include "gl2_dec.h"
#include "SocketStream.h"
#include <functional>

// Only the commands used by the P1 test are exposed, not a full guest EGL stack.
class ProbeDecoder {
public:
    explicit ProbeDecoder(std::function<void()> frameBoundary = {});
    bool next(SocketStream& stream);
    unsigned int packets() const { return packets_; }
    unsigned int frames() const { return frames_; }

private:
    struct Context : gl2_decoder_context_t {
        std::function<void()> frameBoundary;
    } decoder_{};
    ChecksumCalculator checksum_;
    unsigned int packets_ = 0;
    unsigned int frames_ = 0;
};
