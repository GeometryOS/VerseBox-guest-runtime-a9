#include "HostAudioPcm.h"
#include <stdint.h>
#include <string.h>

bool hostPcm16Conversion(audio_format_t format) {
    return format == AUDIO_FORMAT_PCM_24_BIT_PACKED || format == AUDIO_FORMAT_PCM_8_24_BIT ||
        format == AUDIO_FORMAT_PCM_32_BIT;
}

void toHostPcm16(const void* source, void* destination, size_t samples, audio_format_t format) {
    const auto* input = static_cast<const unsigned char*>(source);
    auto* output = static_cast<unsigned char*>(destination);
    for (size_t i = 0; i < samples; ++i) {
        int16_t value;
        if (format == AUDIO_FORMAT_PCM_24_BIT_PACKED) {
            value = static_cast<int16_t>(input[i * 3 + 1] | (input[i * 3 + 2] << 8));
        } else {
            int32_t sample;
            memcpy(&sample, input + i * 4, 4);
            value = static_cast<int16_t>(sample >> (format == AUDIO_FORMAT_PCM_32_BIT ? 16 : 8));
        }
        memcpy(output + i * 2, &value, 2);
    }
}

void fromHostPcm16(const void* source, void* destination, size_t samples, audio_format_t format) {
    const auto* input = static_cast<const unsigned char*>(source);
    auto* output = static_cast<unsigned char*>(destination);
    for (size_t i = 0; i < samples; ++i) {
        int16_t sample;
        memcpy(&sample, input + i * 2, 2);
        if (format == AUDIO_FORMAT_PCM_24_BIT_PACKED) {
            output[i * 3] = 0;
            output[i * 3 + 1] = static_cast<uint16_t>(sample);
            output[i * 3 + 2] = static_cast<uint16_t>(sample) >> 8;
        } else {
            uint32_t value = static_cast<uint32_t>(static_cast<int32_t>(sample)) <<
                (format == AUDIO_FORMAT_PCM_32_BIT ? 16 : 8);
            memcpy(output + i * 4, &value, 4);
        }
    }
}
