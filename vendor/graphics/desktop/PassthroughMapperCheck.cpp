#include <android/hardware/graphics/mapper/2.0/IMapper.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils/StrongPointer.h>

extern "C" void* android_load_sphal_library(const char*, int);

using android::hardware::graphics::mapper::V2_0::IMapper;
using android::hardware::graphics::mapper::V2_0::Error;

int main(int argc, char** argv) {
    setbuf(stdout, nullptr);
    const char* library = argc > 1 ? argv[1] :
            "/vendor/lib64/hw/android.hardware.graphics.mapper@2.0-impl-qti-display.so";
    void* handle = android_load_sphal_library(library, RTLD_NOW | RTLD_LOCAL);
    if (!handle) { printf("mapper_load_error=%s\n", dlerror()); return 1; }
    auto factory = reinterpret_cast<IMapper* (*)(const char*)>(dlsym(handle, "HIDL_FETCH_IMapper"));
    if (!factory) { printf("mapper_factory=missing\n"); return 1; }
    Dl_info selected{};
    if (dladdr(reinterpret_cast<void*>(factory), &selected))
        printf("mapper_selected=%s\n", selected.dli_fname);
    android::sp<IMapper> mapper = factory("default");
    if (!mapper) { printf("mapper_instance=missing\n"); return 1; }
    IMapper::BufferDescriptorInfo info{};
    info.width = info.height = info.layerCount = 1;
    info.format = android::hardware::graphics::common::V1_0::PixelFormat::RGBA_8888;
    info.usage = 0x200;
    bool compatible = false;
    auto result = mapper->createDescriptor(info, [&](Error error, const auto& descriptor) {
        printf("mapper_error=%d\nmapper_descriptor_words=%zu\n", static_cast<int>(error), descriptor.size());
        for (size_t i = 0; i < descriptor.size(); ++i)
            printf("mapper_descriptor_%zu=0x%08x\n", i, descriptor[i]);
        compatible = error == Error::NONE && descriptor.size() == 7 && descriptor[0] == 0x94870000;
    });
    printf("mapper_paired_descriptor=%s\n", result.isOk() && compatible ? "passed" : "failed");
    return result.isOk() && compatible ? 0 : 1;
}
