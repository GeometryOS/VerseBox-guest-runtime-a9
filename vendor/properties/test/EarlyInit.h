#pragma once
#include <cstdint>

struct EarlyPropertyState {
    int result;
    int sdkLength;
    uint32_t areaSerial;
    unsigned allocations;
    int formatLength;
    int errnoAfter;
    char sdk[92];
    char formatted[32];
};

extern EarlyPropertyState earlyProperties;
