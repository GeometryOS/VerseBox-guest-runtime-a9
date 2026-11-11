#pragma once

#include "SocketStream.h"
#include <cstring>

#ifndef A10VM_CODEC_ID
#error Build with the paired source fingerprint
#endif

namespace probe {
inline constexpr char hello[] = "A10VM-GL2-P1 checksum=1 " A10VM_CODEC_ID;
inline constexpr int width = 64;
inline constexpr int height = 64;
inline constexpr int frames = 32;

inline void handshake(SocketStream& stream, bool client) {
    char received[sizeof(hello)]{};
    if (client) stream.writeFully(hello, sizeof(hello));
    stream.readFully(received, sizeof(received));
    if (std::memcmp(received, hello, sizeof(hello)) != 0) {
        throw std::runtime_error("GL codec fingerprint mismatch");
    }
    if (!client) stream.writeFully(hello, sizeof(hello));
}
}
