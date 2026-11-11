#pragma once
#include <stdint.h>

namespace a10pixels {
void yuvToRgbRow(uint8_t* rgb, const uint8_t* y, const uint8_t* u, const uint8_t* v,
                 int left, int right);
void rgbToYuvRow(uint8_t* y, uint8_t* u, uint8_t* v, const uint8_t* rgb,
                 int left, int right, bool chroma);
}
