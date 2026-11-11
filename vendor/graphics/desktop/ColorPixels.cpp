#include "ColorPixels.h"
#include <GLES3/gl3.h>
#include <arm_neon.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
bool packedRgb(GLenum format, GLenum type) {
    return (format == GL_RGB && type == GL_UNSIGNED_BYTE) ||
           ((format == GL_RGB || format == GL_RGB565) && type == GL_UNSIGNED_SHORT_5_6_5);
}

void packRgb(unsigned char* rgb, const unsigned char* rgba, size_t pixels) {
    size_t i = 0;
    for (; i + 15 < pixels; i += 16) {
        const auto source = vld4q_u8(rgba + i * 4);
        const uint8x16x3_t result = {{source.val[0], source.val[1], source.val[2]}};
        vst3q_u8(rgb + i * 3, result);
    }
    for (; i < pixels; ++i) std::memcpy(rgb + i * 3, rgba + i * 4, 3);
}

void unpackRgb(unsigned char* rgba, const unsigned char* rgb, size_t pixels) {
    size_t i = 0;
    for (; i + 15 < pixels; i += 16) {
        const auto source = vld3q_u8(rgb + i * 3);
        const uint8x16x4_t result = {{source.val[0], source.val[1], source.val[2], vdupq_n_u8(255)}};
        vst4q_u8(rgba + i * 4, result);
    }
    for (; i < pixels; ++i) {
        std::memcpy(rgba + i * 4, rgb + i * 3, 3);
        rgba[i * 4 + 3] = 255;
    }
}
}

void ColorPixels::grow(size_t pixels) {
    if (pixels * 4 > rgba_.size()) rgba_.resize(pixels * 4);
}

void ColorPixels::read(GLint x, GLint y, GLsizei width, GLsizei height,
        GLenum format, GLenum type, void* data) {
    GLint alignment;
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    // renderControl transfers tightly packed rows, including odd RGB widths.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if (width > 0 && height > 0 && packedRgb(format, type)) {
        const size_t pixels = static_cast<size_t>(width) * height;
        grow(pixels);
        // RGBA8 is the portable readback pair for our RGBA color buffers.
        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba_.data());
        auto* output = static_cast<unsigned char*>(data);
        if (type == GL_UNSIGNED_BYTE) packRgb(output, rgba_.data(), pixels);
        else {
            for (size_t i = 0; i < pixels; ++i) {
                const auto* pixel = rgba_.data() + i * 4;
                const uint16_t value = ((pixel[0] >> 3) << 11) | ((pixel[1] >> 2) << 5) | (pixel[2] >> 3);
                std::memcpy(output + i * 2, &value, sizeof(value));
            }
        }
    } else glReadPixels(x, y, width, height, format, type, data);
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
}

void ColorPixels::upload(GLint x, GLint y, GLsizei width, GLsizei height,
        GLenum format, GLenum type, const void* data) {
    GLint alignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (width > 0 && height > 0 && packedRgb(format, type)) {
        const size_t pixels = static_cast<size_t>(width) * height;
        grow(pixels);
        const auto* input = static_cast<const unsigned char*>(data);
        if (type == GL_UNSIGNED_BYTE) unpackRgb(rgba_.data(), input, pixels);
        else {
            for (size_t i = 0; i < pixels; ++i) {
                auto* pixel = rgba_.data() + i * 4;
                uint16_t value;
                std::memcpy(&value, input + i * 2, sizeof(value));
                const unsigned r = value >> 11, g = (value >> 5) & 63, b = value & 31;
                pixel[0] = (r << 3) | (r >> 2);
                pixel[1] = (g << 2) | (g >> 4);
                pixel[2] = (b << 3) | (b >> 2);
                pixel[3] = 255;
            }
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba_.data());
    } else glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, format, type, data);
    // Finish before the scratch buffer is reused or the caller's packet is released.
    glFinish();
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
}
