#include "WindowClient.h"
#include "WindowProtocol.h"
#include "GL2Encoder.h"
#include "SocketStream.h"
#include "TouchState.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <atomic>
#include <mutex>
#include <sys/socket.h>

namespace {
void rectangle(GL2Encoder& encoder, int x, int y, int width, int height,
               float r, float g, float b) {
    encoder.glScissor(&encoder, x, y, width, height);
    encoder.glClearColor(&encoder, r, g, b, 1.f);
    encoder.glClear(&encoder, GL_COLOR_BUFFER_BIT);
}

void bits(GL2Encoder& encoder, uint32_t value, int y) {
    for (int bit = 0; bit < 16; ++bit) {
        const float color = ((value >> bit) & 1) ? 1.f : 0.f;
        rectangle(encoder, 8 + bit * 21, y, 16, 24, color, color, color);
    }
}
}

int runWindowClient(GL2Encoder& encoder, SocketStream& control) {
    using namespace windowprobe;
    uint32_t sequence = 0;
    touchwire::State state;
    std::mutex mutex;
    std::atomic<bool> stopped{false};
    std::string failure;
    std::thread receiver([&] {
        try {
            touchwire::Event event;
            while (control.readOrEof(&event, sizeof(event))) {
                touchwire::validate(event, width, height);
                if (!event.running) {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (event.sequence != state.latest().sequence)
                        throw std::runtime_error("Stop overtook pending touch events");
                    std::printf("CLIENT_WINDOW_STOP_RECEIVED sequence=%u applied=%u\n", event.sequence, state.latest().sequence);
                    stopped = true;
                    return;
                }
                std::lock_guard<std::mutex> lock(mutex);
                state.apply(event, width, height);
                if (event.action == touchwire::Up || event.action == touchwire::Cancel)
                    std::printf("CLIENT_TOUCH_APPLIED sequence=%u active=%zu\n", event.sequence, state.activeCount());
            }
            if (!stopped) throw std::runtime_error("Touch channel disconnected");
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!stopped) failure = error.what();
            stopped = true;
        }
    });
    struct Join {
        std::atomic<bool>& stopped;
        SocketStream& control;
        std::thread& thread;
        ~Join() { stopped = true; shutdown(control.fd(), SHUT_RD); thread.join(); }
    } join{stopped, control, receiver};
    auto deadline = std::chrono::steady_clock::now();
    std::puts("CLIENT_WINDOW_READY");
    uint32_t latestInput = 0;
    while (!stopped) {
        std::this_thread::sleep_until(deadline);
        touchwire::Event input;
        touchwire::Pointer last;
        {
            std::lock_guard<std::mutex> lock(mutex);
            input = state.latest(); last = state.last();
        }
        latestInput = input.sequence;
        const Frame frame{magic, ++sequence, input.sequence, last.x, last.y};
        control.writeFully(&frame, sizeof(frame));
        encoder.glDisable(&encoder, GL_SCISSOR_TEST);
        encoder.glClearColor(&encoder, 0.04f, 0.07f, 0.11f, 1.f);
        encoder.glClear(&encoder, GL_COLOR_BUFFER_BIT);
        encoder.glEnable(&encoder, GL_SCISSOR_TEST);
        bits(encoder, sequence, height - 40);
        bits(encoder, input.sequence, height - 80);
        rectangle(encoder, std::clamp(last.x - 9, 0, width - 18),
                  std::clamp(height - 1 - last.y - 9, 0, height - 18), 18, 18, 1.f, 0.f, 1.f);
        for (uint32_t i = 0; i < input.count && input.action != touchwire::Up && input.action != touchwire::Cancel; ++i) {
            const auto& pointer = input.pointers[i];
            rectangle(encoder, std::clamp(pointer.x - 4, 0, width - 8),
                      std::clamp(height - 1 - pointer.y - 4, 0, height - 8), 8, 8, 0.f, 1.f, 1.f);
        }
        encoder.glFinish(&encoder);
        if (encoder.glGetError(&encoder) != GL_NO_ERROR) throw std::runtime_error("Window GL error");
        deadline += std::chrono::nanoseconds(16666667);
        const auto now = std::chrono::steady_clock::now();
        if (deadline + std::chrono::milliseconds(100) < now) deadline = now;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!failure.empty()) throw std::runtime_error(failure);
    }
    std::printf("CLIENT_WINDOW_DONE frames=%u input=%u\n", sequence, latestInput);
    return 0;
}
