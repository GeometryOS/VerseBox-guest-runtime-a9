#pragma once
#include <EGL/egl.h>
#include <stdint.h>

namespace a10egl {
// EGL reserves null; the renderControl protocol uses zero-based config indices.
inline EGLConfig configHandle(uint32_t index) {
    return reinterpret_cast<EGLConfig>(static_cast<uintptr_t>(index) + 1);
}
inline uint32_t configIndex(EGLConfig config) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(config) - 1);
}
inline bool validConfig(EGLConfig config, int count) {
    const uintptr_t handle = reinterpret_cast<uintptr_t>(config);
    return handle > 0 && count > 0 && handle <= static_cast<uintptr_t>(count);
}
}
