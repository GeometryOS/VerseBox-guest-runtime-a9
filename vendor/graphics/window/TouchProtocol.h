#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace touchwire {
inline constexpr uint32_t magic = 0x54434832;
inline constexpr uint32_t maxPointers = 10;
enum Action : uint32_t { Down = 0, Up = 1, Move = 2, Cancel = 3, PointerDown = 5, PointerUp = 6 };
struct Pointer {
    int32_t id = 0, x = 0, y = 0;
    float pressure = 0;
};
struct Event {
    uint32_t tag = magic, running = 1, sequence = 0, action = Cancel;
    uint32_t actionIndex = 0, count = 0;
    int64_t timeNanos = 0;
    Pointer pointers[maxPointers]{};
};
static_assert(sizeof(Pointer) == 16 && sizeof(Event) == 192);

inline void validate(const Event& event, int width, int height) {
    if (event.tag != magic || event.running > 1 || event.count > maxPointers || event.timeNanos < 0)
        throw std::runtime_error("Invalid touch header");
    if (!event.running) {
        if (event.count != 0) throw std::runtime_error("Invalid touch stop");
        return;
    }
    if (event.action != Down && event.action != Up && event.action != Move &&
        event.action != Cancel && event.action != PointerDown && event.action != PointerUp)
        throw std::runtime_error("Invalid touch action");
    if (event.action == Cancel) {
        if (event.count != 0) throw std::runtime_error("Invalid touch cancel");
        return;
    }
    if (event.count == 0 || event.actionIndex >= event.count ||
        ((event.action == Down || event.action == Up) && event.count != 1) ||
        ((event.action == PointerDown || event.action == PointerUp) && event.count < 2))
        throw std::runtime_error("Invalid touch pointer count/index");
    for (uint32_t i = 0; i < event.count; ++i) {
        const auto& p = event.pointers[i];
        if (p.id < 0 || p.id > 31 || p.x < 0 || p.x >= width || p.y < 0 || p.y >= height ||
            !std::isfinite(p.pressure) || p.pressure < 0)
            throw std::runtime_error("Invalid touch pointer");
        for (uint32_t j = 0; j < i; ++j)
            if (event.pointers[j].id == p.id) throw std::runtime_error("Duplicate touch pointer id");
    }
}
}
