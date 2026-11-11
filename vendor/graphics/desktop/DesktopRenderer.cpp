#include "DesktopRenderer.h"
#include "DesktopDisplay.h"
#include <algorithm>
#include <chrono>
#include <unistd.h>

DesktopRenderer::DesktopRenderer(const std::string& directory, const desktopdisplay::Mode& mode)
    // 先合法化，再把同一组宽高交给 GPU 服务与输入通道：三者必须一致，
    // 否则画面尺寸与触摸换算会对不上。
    : mode_(desktopdisplay::sanitize(mode)),
      gpu_(directory + "/gpu.sock", mode_),
      input_(directory + "/desktop-input.sock", mode_.width, mode_.height) {
    description_ = "Android 10 GLES / host GPU";
}
uint32_t DesktopRenderer::tap(int x, int y) {
    touchwire::Event event;
    event.action = touchwire::Down;
    event.count = 1;
    event.timeNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    event.pointers[0] = {0, std::clamp(x, 0, mode_.width - 1),
                        std::clamp(y, 0, mode_.height - 1), 1.f};
    touch(event);
    event.action = touchwire::Up;
    return touch(event);
}
std::array<int64_t, 17> DesktopRenderer::metrics() {
    auto frame = gpu_.backend().presentation();
    auto input = input_.state();
    std::array<int64_t, 17> result{};
    result[0] = 1;
    result[1] = frame.frames;
    result[2] = frame.swaps;
    result[3] = input[0];
    result[4] = input[1];
    result[5] = frame.swaps ? frame.frames : 0;
    result[6] = frame.requested;
    result[7] = frame.bound;
    result[8] = input[2];
    result[9] = frame.visible;
    result[11] = getpid();
    result[12] = getuid();
    result[15] = geteuid();
    result[16] = frame.presented;
    return result;
}
std::string DesktopRenderer::error() {
    auto graphics = gpu_.error();
    return graphics.empty() ? input_.error() : graphics;
}
std::string DesktopRenderer::description() { return description_; }
