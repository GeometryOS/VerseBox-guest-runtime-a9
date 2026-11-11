#include "DesktopGpuServer.h"
#include <csignal>
#include <chrono>
#include <stdexcept>
#include <cstdio>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t stopped;
volatile sig_atomic_t capture;
void stop(int) { stopped = 1; }
void snapshot(int) { capture = 1; }
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    try {
        if (argc != 2 && argc != 3) throw std::invalid_argument("Usage: desktop-gpu-host SOCKET [CAPTURE_PPM]");
        std::signal(SIGTERM, stop);
        std::signal(SIGINT, stop);
        std::signal(SIGUSR1, snapshot);
        DesktopGpuServer server(argv[1]);
        std::printf("desktop_gpu_pid=%d\ndesktop_gpu_ready=1\n", getpid());
        while (!stopped) {
            if (capture) {
                capture = 0;
                if (argc == 3 && server.backend().capture(argv[2]))
                    std::printf("desktop_gpu_capture=%s\n", argv[2]);
            }
            auto error = server.error();
            if (!error.empty()) throw std::runtime_error(error);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "desktop_gpu_error=%s\n", error.what());
        return 1;
    }
}
