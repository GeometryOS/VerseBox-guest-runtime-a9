#include "HostCameraHal.h"
#include <hardware/camera_common.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HostCameraHal* host(camera_device* device) { return static_cast<HostCameraHal*>(device->priv); }

void HostCameraHal::updateSize() {
    size_t offset = parameters.find("preview-size=");
    if (offset != std::string::npos) sscanf(parameters.c_str() + offset + 13, "%dx%d", &width, &height);
}

int HostCameraHal::open(int id) {
    int32_t status, session;
    if (!control.valid() || !control.put(id) || !control.get(&status)) return -ENODEV;
    if (status) return status;
    if (!control.get(&session) || !control.getString(&parameters, 65536) || !events.valid() || !events.put(session))
        return -EIO;
    updateSize();
    reader = std::thread(&HostCameraHal::readEvents, this);
    return 0;
}

int HostCameraHal::command(int code, const std::string* text, int value) {
    std::lock_guard<std::mutex> guard(commands);
    int32_t status;
    if (!control.put(code) || (text && !control.putString(*text)) ||
            ((code == 8 || code == 10) && !control.put(value)) || !control.get(&status)) return -EIO;
    if (!status && (code == 1 || code == 2)) {
        std::string result;
        if (!control.getString(&result, 65536)) return -EIO;
        std::lock_guard<std::mutex> lock(state);
        parameters = result;
        updateSize();
        if (code == 2) return configureWindow();
    }
    return status;
}

void HostCameraHal::close() {
    preview = false; recording = false;
    control.cancel(); events.cancel();
    if (reader.joinable()) reader.join();
    for (const auto& frame : videoFrames) frame.second->release(frame.second);
    videoFrames.clear();
}

static camera_device_ops_t operations = [] {
    camera_device_ops_t ops{};
    ops.set_preview_window = [](camera_device* device, preview_stream_ops* window) {
        auto* session = host(device);
        std::lock_guard<std::mutex> guard(session->state);
        session->window = window;
        return session->configureWindow();
    };
    ops.set_callbacks = [](camera_device* device, camera_notify_callback notice, camera_data_callback data,
            camera_data_timestamp_callback timestamp, camera_request_memory memory, void* user) {
        auto* session = host(device);
        std::lock_guard<std::mutex> guard(session->state);
        session->notify = notice; session->data = data; session->timestamp = timestamp;
        session->allocate = memory; session->user = user;
    };
    ops.enable_msg_type = [](camera_device* d, int32_t flags) { host(d)->messages |= flags; };
    ops.disable_msg_type = [](camera_device* d, int32_t flags) { host(d)->messages &= ~flags; };
    ops.msg_type_enabled = [](camera_device* d, int32_t flags) { return (host(d)->messages & flags) == flags ? 1 : 0; };
    ops.start_preview = [](camera_device* d) {
        int status = host(d)->command(3);
        if (!status) host(d)->preview = true;
        return status;
    };
    ops.stop_preview = [](camera_device* d) { host(d)->preview = false; host(d)->command(4); };
    ops.preview_enabled = [](camera_device* d) { return host(d)->preview ? 1 : 0; };
    ops.store_meta_data_in_buffers = [](camera_device*, int enabled) { return enabled ? -ENOSYS : 0; };
    ops.start_recording = [](camera_device* d) {
        int status = host(d)->command(3);
        if (!status) { host(d)->preview = true; host(d)->recording = true; }
        return status;
    };
    ops.stop_recording = [](camera_device* d) { host(d)->recording = false; };
    ops.recording_enabled = [](camera_device* d) { return host(d)->recording ? 1 : 0; };
    ops.release_recording_frame = [](camera_device* d, const void* opaque) {
        auto* session = host(d);
        std::lock_guard<std::mutex> guard(session->state);
        auto found = session->videoFrames.find(opaque);
        if (found != session->videoFrames.end()) {
            found->second->release(found->second); session->videoFrames.erase(found);
        }
    };
    ops.auto_focus = [](camera_device* d) { return host(d)->command(5); };
    ops.cancel_auto_focus = [](camera_device* d) { return host(d)->command(6); };
    ops.take_picture = [](camera_device* d) {
        int status = host(d)->command(7);
        if (!status) host(d)->preview = false;
        return status;
    };
    ops.cancel_picture = [](camera_device*) { return 0; };
    ops.set_parameters = [](camera_device* d, const char* parameters) {
        std::string value(parameters ? parameters : "");
        return host(d)->command(2, &value);
    };
    ops.get_parameters = [](camera_device* d) {
        if (host(d)->command(1)) return static_cast<char*>(nullptr);
        std::lock_guard<std::mutex> guard(host(d)->state);
        return strdup(host(d)->parameters.c_str());
    };
    ops.put_parameters = [](camera_device*, char* value) { free(value); };
    ops.send_command = [](camera_device* d, int32_t command, int32_t value, int32_t) {
        switch (command) {
            case CAMERA_CMD_SET_DISPLAY_ORIENTATION: return host(d)->command(8, nullptr, value);
            case CAMERA_CMD_ENABLE_FOCUS_MOVE_MSG: return host(d)->command(10, nullptr, value);
            default: return -ENOSYS;
        }
    };
    ops.release = [](camera_device* d) { host(d)->close(); };
    ops.dump = [](camera_device*, int) { return 0; };
    return ops;
}();

static int cameraCount() {
    HostDeviceSocket socket("camera", 0x41313043, 1);
    int32_t count = 0;
    return socket.get(&count) && count >= 0 && count <= 32 ? count : 0;
}

static int cameraInfo(int id, camera_info* info) {
    HostDeviceSocket socket("camera", 0x41313043, 1);
    int32_t count = 0;
    if (!info || !socket.get(&count) || id < 0 || id >= count) return -EINVAL;
    int32_t facing, orientation;
    for (int i = 0; i <= id; ++i) if (!socket.get(&facing) || !socket.get(&orientation)) return -EIO;
    memset(info, 0, sizeof(*info));
    info->facing = facing; info->orientation = orientation;
    info->device_version = CAMERA_DEVICE_API_VERSION_1_0;
    info->resource_cost = 100;
    return 0;
}

static hw_module_methods_t methods = {[](const hw_module_t* module, const char* name, hw_device_t** output) {
    if (!name || !output) return -EINVAL;
    char* end;
    long id = strtol(name, &end, 10);
    if (*end || id < 0 || id >= cameraCount()) return -EINVAL;
    auto* session = new HostCameraHal;
    int status = session->open(id);
    if (status) { session->close(); delete session; return status; }
    session->device.common.tag = HARDWARE_DEVICE_TAG;
    session->device.common.version = CAMERA_DEVICE_API_VERSION_1_0;
    session->device.common.module = const_cast<hw_module_t*>(module);
    session->device.common.close = [](hw_device_t* device) {
        auto* session = host(reinterpret_cast<camera_device*>(device));
        session->close(); delete session; return 0;
    };
    session->device.ops = &operations;
    session->device.priv = session;
    *output = &session->device.common;
    return 0;
}};

extern "C" camera_module_t HAL_MODULE_INFO_SYM = [] {
    camera_module_t module{};
    module.common.tag = HARDWARE_MODULE_TAG;
    module.common.module_api_version = CAMERA_MODULE_API_VERSION_2_4;
    module.common.hal_api_version = HARDWARE_HAL_API_VERSION;
    module.common.id = CAMERA_HARDWARE_MODULE_ID;
    module.common.name = "A10VM host camera";
    module.common.author = "A10VM";
    module.common.methods = &methods;
    module.get_number_of_cameras = cameraCount;
    module.get_camera_info = cameraInfo;
    module.set_callbacks = [](const camera_module_callbacks_t*) { return 0; };
    module.get_vendor_tag_ops = [](vendor_tag_ops_t* ops) { memset(ops, 0, sizeof(*ops)); };
    module.set_torch_mode = [](const char*, bool) { return -ENOSYS; };
    module.init = [] { return 0; };
    return module;
}();
