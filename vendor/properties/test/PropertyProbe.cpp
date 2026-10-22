#include "ProbeChecks.h"
#include "ProcessIdentity.h"
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

static void timeoutCheck(PrivateProperties& properties, const prop_info* property,
                         const char* name) {
    uint32_t before = property ? properties.serial(property) : properties.areaSerial();
    uint32_t after = 0xdeadbeef;
    timespec timeout{0, 40000000};
    auto start = std::chrono::steady_clock::now();
    bool changed = properties.wait(property, before, &after, &timeout);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
    require(!changed && after == 0xdeadbeef && elapsed >= 30000 && elapsed < 1000000, name);
    std::printf("%s_us=%lld\n", name, static_cast<long long>(elapsed));
}

static void lifecycleChecks(const std::string& directory) {
    int baseline = mappingCount(directory);
    for (int i = 0; i < 20; ++i) {
        PrivateProperties reader;
        require(reader.open(directory), "reopen");
        require(reader.get("ro.build.version.sdk") == "29", "reopen_read");
    }
    require(mappingCount(directory) == baseline, "reopen_unmapped");
    auto info = directory + "/property_info";
    require(chmod(info.c_str(), 0464) == 0, "info_make_group_writable");
    { PrivateProperties reader; require(!reader.open(directory), "info_writable_rejected"); }
    require(chmod(info.c_str(), 0444) == 0, "info_restore_mode");
    require(mappingCount(directory) == baseline, "failed_open_unmapped");
    auto area = directory + "/a10vm_private_prop";
    auto saved = directory + "/saved_area";
    require(rename(area.c_str(), saved.c_str()) == 0, "area_rename");
    require(symlink(saved.c_str(), area.c_str()) == 0, "area_symlink");
    {
        PrivateProperties reader;
        require(reader.open(directory) && reader.get("ro.build.version.sdk").empty(),
                "area_symlink_rejected");
    }
    require(unlink(area.c_str()) == 0 && rename(saved.c_str(), area.c_str()) == 0,
            "area_restore");
}

static void run(const char* directory, const char* nonce, const char* program, const char* linker) {
    printIdentity(nonce);
    PrivateProperties writer;
    require(writer.create(directory), "writer_create");
    require(writer.set("ro.build.version.sdk", "29"), "sdk_seed");
    require(writer.get("ro.build.version.sdk") == "29", "private_sdk29");
    std::printf("private_sdk=%s\n", writer.get("ro.build.version.sdk").c_str());
    require(!writer.set("ro.build.version.sdk", "36") &&
            writer.get("ro.build.version.sdk") == "29", "readonly_rewrite_rejected");
    require(writer.set("probe.state", "initial"), "mutable_seed");
    std::string longValue(400, 'x');
    require(writer.set("ro.probe.long", longValue) &&
            writer.get("ro.probe.long") == longValue, "long_readonly_callback");
    require(!writer.set("probe.long", longValue) && writer.find("probe.long") == nullptr,
            "long_mutable_rejected");
    require(writer.get("probe.absent").empty(), "absent_empty");
    {
        PrivateProperties reader;
        require(reader.open(directory), "local_reader_open");
        require(reader.get("ro.probe.long") == longValue && reader.count() == 3,
                "reader_callback_and_foreach");
        timeoutCheck(reader, reader.find("probe.state"), "property_timeout");
        timeoutCheck(reader, nullptr, "global_timeout");
    }
    crossProcess(writer, directory, program, linker);
    require(writer.count() == 4, "writer_foreach");
    auto other = std::string(directory) + "/other";
    {
        PrivateProperties second;
        require(second.create(other) && second.set("ro.build.version.sdk", "29") &&
                second.set("probe.instance", "second"), "second_instance_create");
        PrivateProperties firstReader, secondReader;
        require(firstReader.open(directory) && secondReader.open(other), "two_readers");
        require(firstReader.get("probe.instance").empty() &&
                secondReader.get("probe.instance") == "second" &&
                secondReader.get("probe.state").empty(), "instances_isolated");
    }
    require(mappingCount(other) == 0, "second_instance_unmapped");
    lifecycleChecks(directory);
    require(mappingCount(directory, "rw-s") == 2, "writer_shared_mappings");
    char sdk[PROP_VALUE_MAX]{};
    __system_property_get("ro.build.version.sdk", sdk);
    std::printf("host_sdk_after=%s\n", sdk);
    std::puts("probe_complete=1");
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    try {
        guardParent();
        if (argc == 4 && std::strcmp(argv[1], "reader") == 0) {
            return readerMain(argv[2], std::stoi(argv[3]));
        }
        if (argc != 5 || std::strcmp(argv[1], "run") != 0) return 64;
        run(argv[2], argv[3], argv[0], argv[4]);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "property_error=%s\n", error.what());
        return 1;
    }
}
