#include <hardware/hardware.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

extern "C" void* android_load_sphal_library(const char* path, int flags) {
    if (path && getenv("A10VM_ROOTFS")) {
        const char* name = strrchr(path, '/');
        name = name ? name + 1 : path;
        constexpr char prefix[] = "android.hardware.graphics.mapper@2.0-impl";
        size_t length = strlen(name);
        if (!strncmp(name, prefix, sizeof(prefix) - 1) && length >= sizeof(prefix) + 2 &&
                (name[sizeof(prefix) - 1] == '.' || name[sizeof(prefix) - 1] == '-') &&
                !strcmp(name + length - 3, ".so"))
            return dlopen("android.hardware.graphics.mapper@2.0-impl-2.1.so", flags);
    }
    using Load = void* (*)(const char*, int);
    auto original = reinterpret_cast<Load>(dlsym(RTLD_NEXT, "android_load_sphal_library"));
    return original ? original(path, flags) : nullptr;
}

extern "C" int hw_get_module_by_class(const char* id, const char* instance,
                                      const hw_module_t** module) {
    if (!id || !module) return -EINVAL;
    const char* name = nullptr;
    if (getenv("A10VM_ROOTFS")) {
        if (!instance && !strcmp(id, "gralloc")) name = "gralloc.ranchu.so";
        else if (!instance && !strcmp(id, "camera")) name = "camera.a10vm.so";
        else if (!instance && !strcmp(id, "hwcomposer")) name = "hwcomposer.ranchu.so";
        else if (!strcmp(id, "audio") && instance && !strcmp(instance, "primary")) name = "audio.primary.a10vm.so";
        else if (!strcmp(id, "audio") && instance && !strcmp(instance, "r_submix")) name = "audio.r_submix.default.so";
    }
    if (name) {
        // These modules are verified APK native libraries, outside the virtual vendor tree.
        void* library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        *module = nullptr;
        if (!library) return -EINVAL;
        auto info = static_cast<hw_module_t*>(dlsym(library, HAL_MODULE_INFO_SYM_AS_STR));
        if (!info || !info->id || strcmp(info->id, id)) {
            dlclose(library);
            return -EINVAL;
        }
        info->dso = library;
        *module = info;
        return 0;
    }
    using GetModule = int (*)(const char*, const char*, const hw_module_t**);
    auto original = reinterpret_cast<GetModule>(dlsym(RTLD_NEXT, "hw_get_module_by_class"));
    return original ? original(id, instance, module) : -ENOENT;
}

extern "C" int hw_get_module(const char* id, const hw_module_t** module) {
    return hw_get_module_by_class(id, nullptr, module);
}
