#pragma once

/**
 * 桌面显示几何：编译期默认值 + **每会话一份**的运行时模式。
 *
 * 为什么不是一个进程全局值：同一进程里可以同时跑多台虚拟机（MULTI_VM），
 * 它们的分辨率互不相同。全局值会让"后创建的那台"改掉"先创建的那台"的几何，
 * 表现为一台画面正常、另一台被拉伸。所以模式挂在渲染器上，一路传到
 * GpuBackend（帧缓冲尺寸 + 上报客体的 DPI/刷新率）与 DesktopInputChannel（触摸校验）。
 *
 * `width/height/density/refreshHz` 仍是编译期默认值：配置缺失、非法、或旧调用点
 * 不传值时行为与改造前逐字相同。
 */
namespace desktopdisplay {
constexpr int width = 1080;
constexpr int height = 2160;
constexpr int density = 480;
constexpr int refreshHz = 120;
constexpr int vsyncNanos = 1000000000 / refreshHz;

/** 一次运行使用的显示模式（宿主帧缓冲尺寸 + 上报给客体的 DPI/刷新率）。 */
struct Mode {
    int width = desktopdisplay::width;
    int height = desktopdisplay::height;
    int density = desktopdisplay::density;
    int refreshHz = desktopdisplay::refreshHz;
};

/**
 * 合法化：非法输入一律回落到编译期默认值。
 *
 * 与 Kotlin 侧 `DisplayGeometry.isValid` 同一组边界（320..7680 / 120..960）。
 * 这里是最后一道闸：库被手改、旧版本写进垃圾值时，宁可回默认也不让渲染器崩。
 */
inline Mode sanitize(Mode mode) {
    const bool size = mode.width >= 320 && mode.width <= 7680 &&
                      mode.height >= 320 && mode.height <= 7680;
    const bool dpi = mode.density >= 120 && mode.density <= 960;
    if (!size || !dpi) return Mode{};
    if (mode.refreshHz <= 0) mode.refreshHz = refreshHz;
    return mode;
}
}  // namespace desktopdisplay
