#include "EarlyInit.h"
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include <async_safe/log.h>
#include <cerrno>
#include <cstdlib>
#include <unistd.h>

EarlyPropertyState earlyProperties;
static bool trackingAllocations;

extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);

extern "C" void* __wrap_malloc(size_t size) {
    if (trackingAllocations) ++earlyProperties.allocations;
    return __real_malloc(size);
}

extern "C" void* __wrap_calloc(size_t count, size_t size) {
    if (trackingAllocations) ++earlyProperties.allocations;
    return __real_calloc(count, size);
}

extern "C" void* __wrap_realloc(void* memory, size_t size) {
    if (trackingAllocations) ++earlyProperties.allocations;
    return __real_realloc(memory, size);
}

static void initializeBeforeConstructors(int, char**, char** environment) {
    // __libc_init_common supplies environ before calling __system_properties_init.
    environ = environment;
    trackingAllocations = true;
    errno = EDOM;
    earlyProperties.result = __system_properties_init();
    earlyProperties.errnoAfter = errno;
    earlyProperties.sdkLength = __system_property_get("ro.build.version.sdk", earlyProperties.sdk);
    earlyProperties.areaSerial = __system_property_area_serial();
    earlyProperties.formatLength = async_safe_format_buffer(
            earlyProperties.formatted, sizeof(earlyProperties.formatted), "%s:%d:%08x",
            "early", -29, 0x1234);
    trackingAllocations = false;
}

// Run the actual API before the executable's C++ initializers, without stdio or heap use.
__attribute__((section(".preinit_array"), used))
static void (*preinitProperties)(int, char**, char**) = initializeBeforeConstructors;
