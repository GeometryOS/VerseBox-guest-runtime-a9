#include <hardware/gralloc.h>
#include <hardware/hwcomposer2.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <sys/resource.h>
#include <unistd.h>

namespace {
void require(bool value, const char* name) {
    std::printf("CHECK\t%s\t%d\n", name, value);
    if (!value) std::exit(1);
}
template<class Function> Function hook(hwc2_device_t* device, int32_t name) {
    auto function = device->getFunction(device, name);
    require(function != nullptr, "hwc_function_available");
    return reinterpret_cast<Function>(function);
}
struct Callbacks { hwc2_display_t display = 0; std::atomic<int> ticks{0}; };
long switches() { rusage usage{}; getrusage(RUSAGE_SELF, &usage); return usage.ru_nvcsw; }
}

void checkHwcIdle(hwc2_device_t* device) {
    Callbacks callbacks;
    auto registration = hook<HWC2_PFN_REGISTER_CALLBACK>(device, HWC2_FUNCTION_REGISTER_CALLBACK);
    registration(device, HWC2_CALLBACK_HOTPLUG, &callbacks,
        reinterpret_cast<hwc2_function_pointer_t>(+[](void* data, hwc2_display_t id, int32_t) {
            static_cast<Callbacks*>(data)->display = id;
        }));
    registration(device, HWC2_CALLBACK_VSYNC, &callbacks,
        reinterpret_cast<hwc2_function_pointer_t>(+[](void* data, hwc2_display_t, int64_t) {
            ++static_cast<Callbacks*>(data)->ticks;
        }));
    auto power = hook<HWC2_PFN_SET_POWER_MODE>(device, HWC2_FUNCTION_SET_POWER_MODE);
    auto vsync = hook<HWC2_PFN_SET_VSYNC_ENABLED>(device, HWC2_FUNCTION_SET_VSYNC_ENABLED);
    require(power(device, callbacks.display, HWC2_POWER_MODE_ON) == 0, "hwc_power_on");
    require(vsync(device, callbacks.display, HWC2_VSYNC_ENABLE) == 0, "hwc_vsync_enable");
    usleep(60000);
    require(callbacks.ticks >= 2, "hwc_vsync_active");
    vsync(device, callbacks.display, HWC2_VSYNC_DISABLE);
    usleep(20000);
    int paused = callbacks.ticks;
    long before = switches();
    usleep(150000);
    std::printf("hwc_idle_switches=%ld\n", switches() - before);
    require(callbacks.ticks == paused, "hwc_disabled_has_no_callbacks");
    require(switches() - before <= 4, "hwc_disabled_thread_blocks");
    power(device, callbacks.display, HWC2_POWER_MODE_OFF);
    vsync(device, callbacks.display, HWC2_VSYNC_ENABLE);
    usleep(20000);
    paused = callbacks.ticks;
    usleep(60000);
    require(callbacks.ticks == paused, "hwc_power_off_has_no_callbacks");
    power(device, callbacks.display, HWC2_POWER_MODE_ON);
    usleep(60000);
    require(callbacks.ticks >= paused + 2, "hwc_resume_restarts_vsync");
    vsync(device, callbacks.display, HWC2_VSYNC_DISABLE);

    const hw_module_t* module = nullptr;
    require(hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0, "hwc_target_gralloc");
    alloc_device_t* allocator = nullptr;
    require(gralloc_open(module, &allocator) == 0, "hwc_target_allocator");
    buffer_handle_t target = nullptr;
    int stride = 0;
    require(allocator->alloc(allocator, 32, 32, HAL_PIXEL_FORMAT_RGBA_8888,
            GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_FB,
            &target, &stride) == 0, "hwc_target_buffer");
    auto setTarget = hook<HWC2_PFN_SET_CLIENT_TARGET>(device, HWC2_FUNCTION_SET_CLIENT_TARGET);
    auto validate = hook<HWC2_PFN_VALIDATE_DISPLAY>(device, HWC2_FUNCTION_VALIDATE_DISPLAY);
    auto present = hook<HWC2_PFN_PRESENT_DISPLAY>(device, HWC2_FUNCTION_PRESENT_DISPLAY);
    for (int generation = 0; generation < 2; ++generation) {
        require(setTarget(device, callbacks.display, target, -1, 0, {0, nullptr}) == 0, "hwc_fresh_target");
        for (int index = 0; index < 150; ++index) {
            uint32_t types = 0, requests = 0;
            int32_t fence = -1;
            require(validate(device, callbacks.display, &types, &requests) == 0, "hwc_target_validated");
            require(present(device, callbacks.display, &fence) == 0, "hwc_target_presented");
            if (fence >= 0) close(fence);
        }
    }
    require(allocator->free(allocator, target) == 0, "hwc_target_freed");
    require(gralloc_close(allocator) == 0, "hwc_target_allocator_closed");
    require(device->common.close(&device->common) == 0, "hwc_waiting_thread_closes");
    std::puts("desktop_hwc_idle_complete=1");
}
