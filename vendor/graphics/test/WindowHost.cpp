#include "WindowRenderer.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>

using namespace std::chrono;
int64_t now() { return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(); }
template<class F> void await(WindowRenderer& renderer, F condition) {
    auto deadline = steady_clock::now() + seconds(8);
    while (!condition()) {
        if (!renderer.error().empty()) throw std::runtime_error(renderer.error());
        if (steady_clock::now() > deadline) throw std::runtime_error("Native window test timeout");
        std::this_thread::sleep_for(milliseconds(5));
    }
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    try {
        if (argc < 2 || argc > 3) throw std::runtime_error("Usage: window-host DIRECTORY [--stop-pending|--stall-input]");
        const bool stopPending = argc == 3 && std::string(argv[2]) == "--stop-pending";
        const bool stall = argc == 3 && std::string(argv[2]) == "--stall-input";
        if (argc == 3 && !stopPending && !stall) throw std::runtime_error("Unknown window test mode");
        const std::string directory = argv[1];
        auto mark = [&](const char* name) {
            auto* file = std::fopen((directory + "/" + name).c_str(), "w");
            if (!file) throw std::runtime_error("Test marker creation failed");
            std::fclose(file);
        };
        auto marked = [&](const char* name) { return access((directory + "/" + name).c_str(), F_OK) == 0; };
        WindowRenderer renderer(argv[1]);
        await(renderer, [&] { return renderer.metrics()[WindowRenderer::Ready]; });
        std::printf("HOST_WINDOW_READY %s\n", renderer.description().c_str());
        await(renderer, [&] { return renderer.metrics()[WindowRenderer::Frames] >= 2; });
        if (stall) {
            mark("stall.begin");
            await(renderer, [&] { return marked("stall.ready"); });
            std::this_thread::sleep_for(milliseconds(100));
        }
        touchwire::Event event;
        event.action = touchwire::Down; event.count = 1; event.timeNanos = now();
        event.pointers[0] = {7, 50, 60, 0.8f};
        renderer.touch(event);
        event.action = touchwire::PointerDown; event.count = 2; event.actionIndex = 1;
        event.pointers[1] = {2, 150, 160, 0.6f}; renderer.touch(event);
        event.action = touchwire::Move; event.actionIndex = 0;
        for (int move = 0; move < 200; ++move) {
            std::swap(event.pointers[0], event.pointers[1]);
            event.timeNanos = now(); renderer.touch(event);
        }
        event.action = touchwire::PointerUp; event.actionIndex = 1; renderer.touch(event);
        event.action = touchwire::Up; event.count = 1; event.actionIndex = 0; renderer.touch(event);
        event.action = touchwire::Cancel; event.count = 0; renderer.touch(event);
        if (stall) {
            mark("stall.input-sent");
            await(renderer, [&] { return marked("stall.release"); });
        }
        if (!stopPending) await(renderer, [&] { auto m = renderer.metrics(); return m[WindowRenderer::EchoSequence] == 205 && m[WindowRenderer::Frames] >= 50; });
        auto metrics = renderer.metrics();
        if (!renderer.requestExit()) throw std::runtime_error("Native window stop write failed");
        await(renderer, [&] { return renderer.metrics()[WindowRenderer::Done]; });
        std::printf("HOST_WINDOW_OK frames=%lld input=%lld echo=%lld swaps=%lld\n",
            static_cast<long long>(metrics[WindowRenderer::Frames]), static_cast<long long>(metrics[WindowRenderer::InputSequence]),
            static_cast<long long>(metrics[WindowRenderer::EchoSequence]), static_cast<long long>(metrics[WindowRenderer::Swaps]));
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "HOST_WINDOW_ERROR %s\n", error.what()); return 1; }
}
