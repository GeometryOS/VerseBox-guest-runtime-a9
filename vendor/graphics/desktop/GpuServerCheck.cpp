#include "DesktopGpuServer.h"
#include "DesktopProtocol.h"
#include "VsyncSchedule.h"
#include "renderControl_opcodes.h"
#include "ChecksumCalculator.h"
#include <csignal>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <poll.h>
#include <sys/wait.h>
#include <thread>
#include <vector>
#include <unistd.h>

static void require(bool passed, const char* name) {
    std::printf("CHECK\t%s\t%d\n", name, passed);
    if (!passed) std::exit(1);
}

static bool waitWorkers(DesktopGpuServer& server, size_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (server.workerCount() == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

static uint32_t request(SocketStream& stream, ChecksumCalculator& checksum,
                        uint32_t opcode, std::initializer_list<uint32_t> parameters) {
    const auto checksumSize = checksum.checksumByteSize();
    std::vector<uint32_t> packet{opcode, static_cast<uint32_t>((2 + parameters.size()) * 4 + checksumSize)};
    packet.insert(packet.end(), parameters);
    std::vector<unsigned char> bytes(packet[1]);
    std::memcpy(bytes.data(), packet.data(), packet.size() * 4);
    checksum.addBuffer(bytes.data(), packet.size() * 4);
    checksum.writeChecksum(bytes.data() + packet.size() * 4, checksumSize);
    stream.writeFully(bytes.data(), bytes.size());
    uint32_t result;
    stream.readFully(&result, sizeof(result));
    checksum.addBuffer(&result, sizeof(result));
    stream.readFully(bytes.data(), checksumSize);
    require(checksum.validate(bytes.data(), checksumSize), "orphan_command_checksum");
    return result;
}

static int orphan(const char* path, bool partial) {
    {
        SocketStream stream(connectLocal(path), 64 * 1024, 0, true);
        desktopgl::handshake(stream, true);
        ChecksumCalculator checksum;
        checksum.setVersion(1);
        require(request(stream, checksum, OP_rcCreateContext, {0, 0, 2}) != 0, "orphan_context_created");
        require(request(stream, checksum, OP_rcCreateWindowSurface, {0, 8, 8}) != 0, "orphan_surface_created");
    }
    if (partial) {
        SocketStream stream(connectLocal(path), 64 * 1024, 0, true);
        desktopgl::handshake(stream, true);
        const uint32_t opcode = OP_rcMakeCurrent;
        stream.writeFully(&opcode, sizeof(opcode));
        while (true) pause();
    }
    while (true) pause();
}

static bool waitResources(DesktopGpuServer& server, size_t contexts, size_t surfaces) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto count = server.backend().resourceCounts();
        if (count.contexts == contexts && count.surfaces == surfaces && !count.images) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc == 3 && (!std::strcmp(argv[1], "--orphan") || !std::strcmp(argv[1], "--orphan-partial")))
        return orphan(argv[2], !std::strcmp(argv[1], "--orphan-partial"));
    if (argc != 2) return 64;
    const int64_t period = desktopdisplay::vsyncNanos;
    require(desktopvsync::next(period, period + 1000000, period) == 2 * period,
            "vsync_callback_cost_does_not_drift");
    require(desktopvsync::next(period, 6 * period + 1000000, period) == 7 * period,
            "vsync_skips_missed_ticks");
    const auto time = desktopvsync::time(6 * period + 3000000000LL);
    require(desktopvsync::nanos(time) == 6 * period + 3000000000LL, "vsync_timespec_roundtrip");
    int pending = -1;
    {
        DesktopGpuServer server(argv[1]);
        for (int index = 0; index < 4; ++index) {
            const auto child = fork();
            require(child >= 0, "orphan_process_started");
            if (!child) {
                execl(argv[0], argv[0], index % 2 ? "--orphan-partial" : "--orphan", argv[1], nullptr);
                _exit(64);
            }
            const bool created = waitResources(server, 1, 1) && waitWorkers(server, index % 2);
            kill(child, SIGKILL);
            int status;
            require(waitpid(child, &status, 0) == child, "orphan_process_reaped");
            require(created, "live_process_objects_survive_connection_close");
            require(waitResources(server, 0, 0), "dead_process_gpu_objects_reclaimed");
            require(server.error().empty(), "process_death_does_not_poison_gpu_server");
        }
        for (int index = 0; index < 32; ++index) {
            {
                SocketStream stream(connectLocal(argv[1]), 64 * 1024, 0, true);
                desktopgl::handshake(stream, true);
            }
            require(waitWorkers(server, 0), "disconnected_gpu_worker_reaped");
        }
        require(server.error().empty(), "gpu_reconnections_have_no_error");
        {
            SocketStream stream(connectLocal(argv[1]), 64 * 1024, 0, true);
            desktopgl::handshake(stream, true);
            ChecksumCalculator checksum;
            checksum.setVersion(1);
            const uint32_t header[] = {OP_rcGetRendererVersion,
                static_cast<uint32_t>(2 * sizeof(uint32_t) + checksum.checksumByteSize())};
            std::vector<unsigned char> packet(header[1]);
            std::memcpy(packet.data(), header, sizeof(header));
            checksum.addBuffer(packet.data(), sizeof(header));
            checksum.writeChecksum(packet.data() + sizeof(header), checksum.checksumByteSize());
            stream.writeFully(packet.data(), packet.size());
            pollfd reply{stream.fd(), POLLIN, 0};
            require(poll(&reply, 1, 1000) > 0 && (reply.revents & POLLIN), "reset_has_unread_reply");
            // Closing with an unread reply resets the peer while this process stays alive.
        }
        require(waitWorkers(server, 0), "reset_gpu_worker_reaped");
        require(server.error().empty(), "live_peer_reset_does_not_poison_gpu_server");
        {
            SocketStream stream(connectLocal(argv[1]), 64 * 1024, 0, true);
            desktopgl::handshake(stream, true);
            ChecksumCalculator checksum;
            checksum.setVersion(1);
            require(request(stream, checksum, OP_rcGetRendererVersion, {}) == 1,
                    "commands_work_after_peer_reset");
        }
        pending = connectLocal(argv[1]);
        require(waitWorkers(server, 1), "shutdown_has_active_gpu_worker");
    }
    char value;
    require(read(pending, &value, 1) == 0, "shutdown_waits_for_gpu_workers");
    close(pending);
    return 0;
}
