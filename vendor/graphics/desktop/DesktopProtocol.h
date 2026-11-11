#pragma once
#include "SocketStream.h"
#include <array>
#include <cstring>

namespace desktopgl {
inline void handshake(SocketStream& stream, bool client) {
    std::array<char, 80> expected{};
    const uint32_t magic = 0x41314750, version = 1;
    std::memcpy(expected.data(), &magic, sizeof(magic));
    std::memcpy(expected.data() + 4, &version, sizeof(version));
    std::memcpy(expected.data() + 8, A10VM_DESKTOP_CODEC_ID, 64);
    std::array<char, 80> actual{};
    if (client) {
        stream.writeFully(expected.data(), expected.size());
        stream.readFully(actual.data(), actual.size());
    } else {
        stream.readFully(actual.data(), actual.size());
        if (actual != expected) throw std::runtime_error("Desktop GPU codec mismatch");
        stream.writeFully(expected.data(), expected.size());
    }
    if (actual != expected) throw std::runtime_error("Desktop GPU codec mismatch");
}
}

