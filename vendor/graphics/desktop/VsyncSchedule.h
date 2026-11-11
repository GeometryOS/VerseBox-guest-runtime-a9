#pragma once
#include <cstdint>
#include <time.h>

namespace desktopvsync {
inline int64_t nanos(const timespec& time) {
    return int64_t(time.tv_sec) * 1000000000 + time.tv_nsec;
}
inline timespec time(int64_t nanos) {
    return {nanos / 1000000000, nanos % 1000000000};
}
inline int64_t next(int64_t deadline, int64_t now, int64_t period) {
    // Keep the original phase and skip missed ticks instead of emitting a burst.
    return deadline + (now < deadline ? 1 : (now - deadline) / period + 1) * period;
}
}
