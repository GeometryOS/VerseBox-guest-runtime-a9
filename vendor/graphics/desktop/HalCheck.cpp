#include <hardware/gralloc.h>
#include <hardware/hwcomposer2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void checkHwcIdle(hwc2_device_t* device);

namespace {
void require(bool value, const char* operation, int status = 0) {
    if (!value) {
        std::fprintf(stderr, "desktop_hal_error=%s status=%d\n", operation, status);
        std::exit(1);
    }
}

void checkYuvReadback(const gralloc_module_t* gralloc, alloc_device_t* allocator, int format) {
    constexpr int width = 576, height = 288;
    int usage = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_HW_TEXTURE;
    buffer_handle_t buffer = nullptr;
    int stride = 0;
    int status = allocator->alloc(allocator, width, height, format, usage, &buffer, &stride);
    require(status == 0 && buffer, "video YUV allocation", status);
    android_ycbcr planes{};
    status = gralloc->lock_ycbcr(gralloc, buffer, GRALLOC_USAGE_SW_WRITE_OFTEN,
                               0, 0, width, height, &planes);
    require(status == 0 && planes.y && planes.cb && planes.cr, "video YUV write lock", status);
    std::memset(planes.y, 128, planes.ystride * height);
    std::memset(planes.cb, 128, planes.cstride * (height / 2));
    std::memset(planes.cr, 128, planes.cstride * (height / 2));
    require(gralloc->unlock(gralloc, buffer) == 0, "video YUV upload");
    status = gralloc->lock_ycbcr(gralloc, buffer, GRALLOC_USAGE_SW_READ_OFTEN,
                               0, 0, width, height, &planes);
    require(status == 0, "video YUV readback lock", status);
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            size_t offset = y * planes.cstride + x * planes.chroma_step;
            require(static_cast<unsigned char*>(planes.cb)[offset] == 128 &&
                    static_cast<unsigned char*>(planes.cr)[offset] == 128, "video YUV chroma readback");
        }
    }
    require(gralloc->unlock(gralloc, buffer) == 0, "video YUV readback unlock");
    require(allocator->free(allocator, buffer) == 0, "video YUV free");
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const hw_module_t* module = nullptr;
    int status = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
    require(status == 0 && module, "gralloc module", status);
    alloc_device_t* allocator = nullptr;
    status = gralloc_open(module, &allocator);
    require(status == 0 && allocator, "gralloc open", status);
    buffer_handle_t buffer = nullptr;
    int stride = 0;
    status = allocator->alloc(allocator, 32, 32, HAL_PIXEL_FORMAT_RGBA_8888,
        GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE, &buffer, &stride);
    require(status == 0 && buffer && stride >= 32, "gralloc allocation", status);
    status = allocator->free(allocator, buffer);
    require(status == 0, "gralloc free", status);
    const auto* gralloc = reinterpret_cast<const gralloc_module_t*>(module);
    int usage = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_HW_TEXTURE;
    status = allocator->alloc(allocator, 32, 32, HAL_PIXEL_FORMAT_YCbCr_420_888,
        usage, &buffer, &stride);
    require(status == 0 && buffer, "software YUV allocation", status);
    android_ycbcr planes{};
    status = gralloc->lock_ycbcr(gralloc, buffer, usage, 0, 0, 32, 32, &planes);
    require(status == 0 && planes.y && planes.cb && planes.cr, "software YUV lock", status);
    std::memset(planes.y, 128, planes.ystride * 32);
    std::memset(planes.cb, 128, planes.cstride * 16);
    std::memset(planes.cr, 128, planes.cstride * 16);
    status = gralloc->unlock(gralloc, buffer);
    require(status == 0, "software YUV upload without DMA", status);
    require(allocator->free(allocator, buffer) == 0, "software YUV free");
    std::puts("desktop_hal_software_yuv=1");
    checkYuvReadback(gralloc, allocator, HAL_PIXEL_FORMAT_YV12);
    checkYuvReadback(gralloc, allocator, HAL_PIXEL_FORMAT_YCbCr_420_888);
    std::puts("desktop_hal_yuv_readback=1");
    require(gralloc_close(allocator) == 0, "gralloc close");
    std::puts("desktop_hal_gralloc=1");
    status = hw_get_module(HWC_HARDWARE_MODULE_ID, &module);
    require(status == 0 && module, "hwcomposer module", status);
    hw_device_t* device = nullptr;
    status = module->methods->open(module, HWC_HARDWARE_COMPOSER, &device);
    require(status == 0 && device && device->version == HWC_DEVICE_API_VERSION_2_0,
            "hwcomposer open", status);
    checkHwcIdle(reinterpret_cast<hwc2_device_t*>(device));
    std::puts("desktop_hal_composer=1\ndesktop_hal_complete=1");
    return 0;
}
