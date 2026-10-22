#include "ProbeChecks.h"
#include "ProcessIdentity.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
enum Phase { PropertyReady = 1, PropertyChanged, PrepareGlobalWait, GlobalReady, GlobalChanged };

struct Message {
    int phase;
    int pid;
    int uid;
    uint32_t oldSerial;
    uint32_t newSerial;
};

void sendMessage(int fd, const Message& message) {
    if (send(fd, &message, sizeof(message), MSG_NOSIGNAL) != sizeof(message)) {
        throw std::runtime_error("reader send");
    }
}

Message receiveMessage(int fd) {
    pollfd ready{fd, POLLIN, 0};
    Message message{};
    if (poll(&ready, 1, 3000) != 1 ||
        recv(fd, &message, sizeof(message), 0) != sizeof(message)) {
        throw std::runtime_error("reader receive timeout");
    }
    return message;
}
}

int readerMain(const char* directory, int channel) {
    PrivateProperties reader;
    require(reader.open(directory), "reader_open");
    require(reader.get("ro.build.version.sdk") == "29", "reader_sdk29");
    require(!reader.set("probe.state", "bad"), "reader_write_rejected");
    printIdentity("reader");
    auto readMaps = mappingCount(directory, "r--s");
    auto writeMaps = mappingCount(directory, "rw-s");
    std::printf("reader_directory=%s\nreader_read_maps=%d\nreader_write_maps=%d\n",
                directory, readMaps, writeMaps);
    require(readMaps == 3 && writeMaps == 0,
            "reader_readonly_mappings");
    const auto* property = reader.find("probe.state");
    require(property != nullptr, "reader_find");
    uint32_t before = reader.serial(property), after = 0;
    timespec timeout{2, 0};
    auto start = std::chrono::steady_clock::now();
    sendMessage(channel, {PropertyReady, getpid(), static_cast<int>(getuid()), before, 0});
    require(reader.wait(property, before, &after, &timeout) && after != before &&
            reader.get("probe.state") == "updated", "cross_process_property_wait");
    auto propertyWait = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
    require(propertyWait >= 15000, "property_wait_blocked");
    std::printf("property_wait_us=%lld\n", static_cast<long long>(propertyWait));
    sendMessage(channel, {PropertyChanged, getpid(), static_cast<int>(getuid()), before, after});
    // Property wake precedes the writer's global serial update; sample after set() returns.
    require(receiveMessage(channel).phase == PrepareGlobalWait, "writer_global_update_complete");
    auto global = reader.areaSerial();
    start = std::chrono::steady_clock::now();
    sendMessage(channel, {GlobalReady, getpid(), static_cast<int>(getuid()), global, 0});
    require(reader.wait(nullptr, global, &after, &timeout) && after != global &&
            reader.get("probe.new") == "visible", "cross_process_global_wait");
    auto globalWait = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
    require(globalWait >= 15000, "global_wait_blocked");
    std::printf("global_wait_us=%lld\n", static_cast<long long>(globalWait));
    sendMessage(channel, {GlobalChanged, getpid(), static_cast<int>(getuid()), global, after});
    return 0;
}

void crossProcess(PrivateProperties& writer, const std::string& directory,
                  const char* program, const char* linker) {
    int channel[2];
    require(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, channel) == 0, "reader_channel");
    // Flush before fork so the independently exec'd reader never duplicates parent output.
    std::fflush(nullptr);
    pid_t child = fork();
    if (child == 0) {
        close(channel[0]);
        char fd[24];
        std::snprintf(fd, sizeof(fd), "%d", channel[1]);
        execl(linker, linker, program, "reader", directory.c_str(), fd, nullptr);
        _exit(127);
    }
    close(channel[1]);
    if (child < 0) { close(channel[0]); throw std::runtime_error("reader fork"); }
    bool reaped = false;
    try {
        Message ready = receiveMessage(channel[0]);
        require(ready.phase == PropertyReady && ready.pid == child && ready.uid == static_cast<int>(getuid()),
                "reader_identity");
        std::printf("reader_pid=%d\nreader_uid=%d\n", ready.pid, ready.uid);
        usleep(30000);
        require(writer.set("probe.state", "updated"), "writer_update");
        auto update = receiveMessage(channel[0]);
        require(update.phase == PropertyChanged && update.oldSerial != update.newSerial,
                "property_serial_changed");
        std::printf("property_serial_before=%u\nproperty_serial_after=%u\n",
                    update.oldSerial, update.newSerial);
        sendMessage(channel[0], {PrepareGlobalWait, getpid(), static_cast<int>(getuid()), 0, 0});
        require(receiveMessage(channel[0]).phase == GlobalReady, "global_wait_ready");
        usleep(30000);
        require(writer.set("probe.new", "visible"), "writer_add");
        auto added = receiveMessage(channel[0]);
        require(added.phase == GlobalChanged && added.oldSerial != added.newSerial, "global_serial_changed");
        std::printf("area_serial_before=%u\narea_serial_after=%u\n",
                    added.oldSerial, added.newSerial);
        int status = 0;
        require(waitpid(child, &status, 0) == child, "reader_reaped");
        reaped = true;
        require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "reader_exit");
    } catch (...) {
        close(channel[0]);
        if (!reaped) { kill(child, SIGKILL); waitpid(child, nullptr, 0); }
        throw;
    }
    close(channel[0]);
}
