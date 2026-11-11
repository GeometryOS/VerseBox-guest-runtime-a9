#pragma once
#include "DesktopGpuServer.h"
#include "DesktopInputChannel.h"
#include <array>
#include <vector>

using WindowHandle = DesktopPresenter::Window;
class DesktopRenderer {
public:
    /**
     * @param mode 本次运行要用的显示几何（宽 / 高 / DPI）。
     *             缺省 = DesktopDisplay.h 的编译期默认值，旧调用点行为不变。
     */
    explicit DesktopRenderer(const std::string& directory,
                             const desktopdisplay::Mode& mode = desktopdisplay::Mode{});
    int64_t attach(WindowHandle window) { return gpu_.attach(std::move(window)); }
    void detach(int64_t generation) { gpu_.detach(generation); }
    uint32_t touch(touchwire::Event event) { return input_.submit(event); }
    uint32_t tap(int x, int y);
    /** 本次会话真正生效的显示几何（native 回读给 Java 用）。 */
    const desktopdisplay::Mode& mode() const { return mode_; }
    bool requestExit() { input_.finish(); return true; }
    std::array<int64_t, 17> metrics();
    std::string error();
    std::string description();
    std::vector<int64_t> intervals() { return {}; }
    void resetTimings() { }
private:
    // **必须声明在 gpu_ 之前**：GpuBackend 构造时读 desktopdisplay::current*()。
    desktopdisplay::Mode mode_;
    DesktopGpuServer gpu_;
    DesktopInputChannel input_;
    std::string description_;
};
