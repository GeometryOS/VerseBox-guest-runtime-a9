#include <android/hardware/camera/provider/2.4/ICameraProvider.h>
#include <hidl/HidlTransportSupport.h>
#include <hardware/camera_common.h>
#include <stdio.h>
#include "CameraDevice_1_0.h"
#include "CameraModule.h"
#include "AshmemAllocator.h"

using android::sp;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_string;
using android::hardware::camera::common::V1_0::Status;
using android::hardware::camera::common::V1_0::helper::CameraModule;
using android::hardware::camera::device::V1_0::implementation::CameraDevice;
using namespace android::hardware::camera::provider::V2_4;

class HostCameraProvider final : public ICameraProvider {
    sp<CameraModule> module;
    sp<ICameraProviderCallback> callback;
    android::SortedVector<std::pair<std::string, std::string>> names;
public:
    explicit HostCameraProvider(camera_module_t* hardware) : module(new CameraModule(hardware)) {
        module->init();
        for (int id = 0; id < module->getNumberOfCameras(); ++id)
            names.add({std::to_string(id), "device@1.0/legacy/" + std::to_string(id)});
    }
    Return<Status> setCallback(const sp<ICameraProviderCallback>& value) override {
        callback = value;
        return value ? Status::OK : Status::ILLEGAL_ARGUMENT;
    }
    Return<void> getVendorTags(getVendorTags_cb result) override { result(Status::OK, {}); return Void(); }
    Return<void> getCameraIdList(getCameraIdList_cb result) override {
        android::hardware::hidl_vec<hidl_string> ids;
        ids.resize(names.size());
        for (size_t i = 0; i < names.size(); ++i) ids[i] = names[i].second;
        result(Status::OK, ids);
        return Void();
    }
    Return<void> isSetTorchModeSupported(isSetTorchModeSupported_cb result) override {
        result(Status::OK, false); return Void();
    }
    Return<void> getCameraDeviceInterface_V1_x(const hidl_string& name,
            getCameraDeviceInterface_V1_x_cb result) override {
        for (const auto& entry : names) {
            if (entry.second == name.c_str()) {
                sp<CameraDevice> device = new CameraDevice(module, entry.first, names);
                if (device->isInitFailed()) result(Status::INTERNAL_ERROR, nullptr);
                else result(Status::OK, device);
                return Void();
            }
        }
        result(Status::ILLEGAL_ARGUMENT, nullptr);
        return Void();
    }
    Return<void> getCameraDeviceInterface_V3_x(const hidl_string&,
            getCameraDeviceInterface_V3_x_cb result) override {
        result(Status::METHOD_NOT_SUPPORTED, nullptr); return Void();
    }
};

int main() {
    android::hardware::configureRpcThreadpool(6, true);
    sp<android::hidl::allocator::V1_0::implementation::AshmemAllocator> allocator =
            new android::hidl::allocator::V1_0::implementation::AshmemAllocator;
    if (allocator->registerAsService("ashmem") != android::OK) return 1;
    const hw_module_t* hardware = nullptr;
    if (hw_get_module(CAMERA_HARDWARE_MODULE_ID, &hardware)) return 1;
    sp<HostCameraProvider> provider = new HostCameraProvider(reinterpret_cast<camera_module_t*>(
            const_cast<hw_module_t*>(hardware)));
    if (provider->registerAsService("legacy/0") != android::OK) return 1;
    printf("host_camera_provider_ready=1\n"); fflush(stdout);
    android::hardware::joinRpcThreadpool();
    return 0;
}
