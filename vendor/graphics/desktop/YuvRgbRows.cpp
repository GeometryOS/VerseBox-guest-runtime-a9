#include "YuvRgbRows.h"
#include <arm_neon.h>
#include <string.h>

namespace a10pixels {
namespace {
uint8_t clamp(int value) { return value < 0 ? 0 : value > 255 ? 255 : value; }

int16x8_t chroma8(const uint8_t* source) {
    uint32_t four;
    memcpy(&four, source, sizeof(four));
    const auto bytes = vreinterpret_u8_u32(vdup_n_u32(four));
    return vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vzip1_u8(bytes, bytes))), vdupq_n_s16(128));
}

uint8x8_t rgb8(int16x8_t y, int16x8_t u, int16x8_t v, int uScale, int vScale) {
    auto low = vmull_n_s16(vget_low_s16(y), 298);
    auto high = vmull_n_s16(vget_high_s16(y), 298);
    low = vmlal_n_s16(vmlal_n_s16(low, vget_low_s16(u), uScale), vget_low_s16(v), vScale);
    high = vmlal_n_s16(vmlal_n_s16(high, vget_high_s16(u), uScale), vget_high_s16(v), vScale);
    // Negative quotients clamp to zero; saturation preserves scalar /256 results.
    return vqmovn_u16(vcombine_u16(vqshrun_n_s32(low, 8), vqshrun_n_s32(high, 8)));
}

void scalarYuv(uint8_t* rgb, int y, int u, int v) {
    const int luma = (y - 16) * 298;
    u -= 128;
    v -= 128;
    rgb[0] = clamp((luma + 409 * v) / 256);
    rgb[1] = clamp((luma - 100 * u - 208 * v) / 256);
    rgb[2] = clamp((luma + 517 * u) / 256);
}

void scalarRgb(uint8_t* y, uint8_t* u, uint8_t* v, const uint8_t* rgb, int x, bool chroma) {
    const int r = rgb[x * 3], g = rgb[x * 3 + 1], b = rgb[x * 3 + 2];
    y[x] = (77 * r + 150 * g + 29 * b) >> 8;
    if (chroma && !(x & 1)) {
        u[x / 2] = clamp(((-43 * r - 85 * g + 128 * b) >> 8) + 128);
        v[x / 2] = clamp(((128 * r - 107 * g - 21 * b) >> 8) + 128);
    }
}

void storeChroma(uint8_t* destination, int16x8_t value) {
    const auto bytes = vmovn_s16(vaddq_s16(vshrq_n_s16(value, 8), vdupq_n_s16(128)));
    const auto even = vuzp1_u8(vreinterpret_u8_s8(bytes), vreinterpret_u8_s8(bytes));
    uint32_t four = vget_lane_u32(vreinterpret_u32_u8(even), 0);
    memcpy(destination, &four, sizeof(four));
}
}

void yuvToRgbRow(uint8_t* rgb, const uint8_t* y, const uint8_t* u, const uint8_t* v,
                 int left, int right) {
    int x = left;
    if ((x & 1) && x <= right) {
        scalarYuv(rgb, y[x], u[x / 2], v[x / 2]);
        ++x;
        rgb += 3;
    }
    for (; x + 7 <= right; x += 8, rgb += 24) {
        const auto luma = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(y + x))), vdupq_n_s16(16));
        const auto cb = chroma8(u + x / 2), cr = chroma8(v + x / 2);
        const uint8x8x3_t result = {{rgb8(luma, cb, cr, 0, 409),
                                   rgb8(luma, cb, cr, -100, -208), rgb8(luma, cb, cr, 517, 0)}};
        vst3_u8(rgb, result);
    }
    for (; x <= right; ++x, rgb += 3) scalarYuv(rgb, y[x], u[x / 2], v[x / 2]);
}

void rgbToYuvRow(uint8_t* y, uint8_t* u, uint8_t* v, const uint8_t* rgb,
                 int left, int right, bool chroma) {
    int x = left;
    if ((x & 1) && x <= right) scalarRgb(y, u, v, rgb, x++, chroma);
    for (; x + 7 <= right; x += 8) {
        const auto channels = vld3_u8(rgb + x * 3);
        const auto r = vmovl_u8(channels.val[0]), g = vmovl_u8(channels.val[1]), b = vmovl_u8(channels.val[2]);
        const auto luma = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r, 77), g, 150), b, 29);
        vst1_u8(y + x, vshrn_n_u16(luma, 8));
        if (chroma) {
            const auto sr = vreinterpretq_s16_u16(r), sg = vreinterpretq_s16_u16(g), sb = vreinterpretq_s16_u16(b);
            storeChroma(u + x / 2, vmlaq_n_s16(vmlaq_n_s16(vmulq_n_s16(sb, 128), sr, -43), sg, -85));
            storeChroma(v + x / 2, vmlaq_n_s16(vmlaq_n_s16(vmulq_n_s16(sr, 128), sg, -107), sb, -21));
        }
    }
    for (; x <= right; ++x) scalarRgb(y, u, v, rgb, x, chroma);
}
}
