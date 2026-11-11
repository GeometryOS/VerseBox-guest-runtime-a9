#pragma once

#include <cstdint>
#include <stdexcept>

namespace windowprobe {
inline constexpr int width = 360;
inline constexpr int height = 640;
inline constexpr uint32_t magic = 0x57494e31;

// Paired ARM64 little-endian endpoints; this is probe control, not renderControl.
struct Input {
    uint32_t tag = magic;
    uint32_t running = 1;
    uint32_t sequence = 0;
    int32_t x = width / 2;
    int32_t y = height / 2;
};
struct Frame {
    uint32_t tag = magic;
    uint32_t sequence = 0;
    uint32_t inputSequence = 0;
    int32_t x = 0;
    int32_t y = 0;
};
static_assert(sizeof(Input) == 20 && sizeof(Frame) == 20);

inline void validate(const Input& input) {
    if (input.tag != magic || input.running > 1 || input.x < 0 || input.x >= width ||
        input.y < 0 || input.y >= height) throw std::runtime_error("Invalid window input");
}

inline void validate(const Frame& frame, uint32_t previous, const Input& sent) {
    if (frame.tag != magic || frame.sequence != previous + 1 ||
        frame.inputSequence != sent.sequence || frame.x != sent.x || frame.y != sent.y) {
        throw std::runtime_error("Window frame/input sequence mismatch");
    }
}

inline void validate(const Frame& frame, uint32_t previous, uint32_t previousEcho, uint32_t latestInput) {
    if (frame.tag != magic || frame.sequence != previous + 1 || frame.inputSequence < previousEcho ||
        frame.inputSequence > latestInput || frame.x < 0 || frame.x >= width || frame.y < 0 || frame.y >= height)
        throw std::runtime_error("Window frame/input sequence mismatch");
}
}
