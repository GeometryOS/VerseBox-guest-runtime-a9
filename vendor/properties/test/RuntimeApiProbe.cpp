#include "EarlyInit.h"
#include "ProbeChecks.h"
#include "ProcessIdentity.h"
#include "PropertyInfo.h"
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include <climits>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

static std::string sdk() {
    char value[PROP_VALUE_MAX]{};
    __system_property_get("ro.build.version.sdk", value);
    return value;
}

static void commonEarlyChecks() {
    require(earlyProperties.allocations == 0, "api_early_no_heap_calls");
    require(earlyProperties.formatLength == 18 &&
            std::strcmp(earlyProperties.formatted, "early:-29:00001234") == 0,
            "api_async_safe_format");
    require(earlyProperties.errnoAfter == EDOM, "api_init_preserves_errno");
    std::printf("api_early_result=%d\napi_early_sdk=%s\napi_early_serial=%u\n"
                "api_early_heap_calls=%u\n", earlyProperties.result, earlyProperties.sdk,
                earlyProperties.areaSerial, earlyProperties.allocations);
}

static void readerChecks(const std::string& directory) {
    require(sdk() == "29", "api_reader_sdk29");
    auto* property = __system_property_find("ro.probe.long");
    require(property != nullptr, "api_find_long");
    std::string value;
    __system_property_read_callback(property,
            [](void* cookie, const char*, const char* text, uint32_t) {
                *static_cast<std::string*>(cookie) = text;
            }, &value);
    require(value == std::string(400, 'x'), "api_long_callback");
    int count = 0;
    require(__system_property_foreach([](const prop_info*, void* cookie) {
                ++*static_cast<int*>(cookie);
            }, &count) == 0 && count == 4, "api_foreach");
    require(mappingCount(directory, "r--s") == 3 && mappingCount(directory, "rw-s") == 0,
            "api_reader_readonly_mappings");
    int baseline = mappingCount(directory);
    for (int i = 0; i < 20; ++i) {
        require(__system_properties_init() == 0 && sdk() == "29", "api_reinit_read");
    }
    require(mappingCount(directory) == baseline, "api_reinit_no_extra_mappings");
    require(setenv("A10VM_PROPERTIES", (directory + "/other").c_str(), 1) == 0,
            "api_switch_env");
    require(__system_properties_init() == -1 && sdk() == "29",
            "api_instance_switch_rejected");
    require(setenv("A10VM_PROPERTIES", directory.c_str(), 1) == 0 &&
            __system_properties_init() == 0, "api_restore_instance");
}

static void failedInitChecks(const std::string& directory) {
    require(earlyProperties.result == -1 && earlyProperties.sdkLength == 0 &&
            earlyProperties.areaSerial == UINT_MAX && sdk().empty(), "api_uninitialized_empty");
    uint32_t after = 0xdeadbeef;
    timespec timeout{};
    require(!__system_property_wait(nullptr, 0, &after, &timeout) && after == 0xdeadbeef,
            "api_uninitialized_wait");
    require(setenv("A10VM_PROPERTIES", "relative", 1) == 0 &&
            __system_property_area_init() == -1, "api_relative_writer_rejected");
    require(unsetenv("A10VM_PROPERTIES") == 0 && __system_property_area_init() == -1,
            "api_missing_writer_rejected");
    require(setenv("A10VM_PROPERTIES", directory.c_str(), 1) == 0 &&
            __system_properties_init() == 0, "api_failed_init_retry");
    readerChecks(directory);
}

static void writerChecks(const std::string& directory) {
    require(earlyProperties.result == -1 && earlyProperties.sdkLength == 0,
            "api_writer_initially_empty");
    require(mkdir(directory.c_str(), 0700) == 0 && writePropertyInfo(directory), "api_writer_info");
    require(setenv("A10VM_PROPERTIES", directory.c_str(), 1) == 0 &&
            __system_property_area_init() == 0, "api_writer_init");
    const char* name = "ro.build.version.sdk";
    require(__system_property_add(name, std::strlen(name), "29", 2) == 0 && sdk() == "29",
            "api_writer_sdk29");
    require(__system_property_add("probe.api", 9, "before", 6) == 0, "api_writer_add");
    auto* property = const_cast<prop_info*>(__system_property_find("probe.api"));
    require(property != nullptr, "api_writer_find");
    auto before = __system_property_serial(property);
    auto global = __system_property_area_serial();
    require(__system_property_update(property, "after", 5) == 0, "api_writer_update");
    timespec timeout{};
    uint32_t after = 0;
    require(__system_property_wait(property, before, &after, &timeout) && after != before,
            "api_writer_property_serial");
    require(__system_property_wait(nullptr, global, &after, &timeout) && after != global,
            "api_writer_global_serial");
    require(mappingCount(directory, "rw-s") == 2, "api_writer_shared_mappings");
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    try {
        guardParent();
        if (argc != 4) return 64;
        const std::string mode = argv[1], directory = argv[2];
        commonEarlyChecks();
        if (mode == "reader") {
            require(earlyProperties.result == 0 && earlyProperties.sdkLength == 2 &&
                    std::strcmp(earlyProperties.sdk, "29") == 0 && sdk() == "29" &&
                    __system_property_area_serial() == earlyProperties.areaSerial,
                    "api_preinit_survives_constructors");
            readerChecks(directory);
        } else if (mode == "writer") {
            writerChecks(directory);
        } else if (mode == "missing" || mode == "relative" || mode == "incomplete") {
            require(mappingCount(directory) == 0, "api_failed_init_no_seed_mappings");
            failedInitChecks(directory);
        } else return 64;
        printIdentity(argv[3]);
        std::printf("api_mode=%s\napi_sdk=%s\napi_complete=1\n", mode.c_str(), sdk().c_str());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "property_api_error=%s\n", error.what());
        return 1;
    }
}
