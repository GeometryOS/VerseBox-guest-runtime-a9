#pragma once
#include <system/audio.h>
#include <stddef.h>

bool hostPcm16Conversion(audio_format_t format);
void toHostPcm16(const void* source, void* destination, size_t samples, audio_format_t format);
void fromHostPcm16(const void* source, void* destination, size_t samples, audio_format_t format);
