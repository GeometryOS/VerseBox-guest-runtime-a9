#include "HostCameraHal.h"
#include <hardware/gralloc.h>
#include <errno.h>
#include <string.h>

int HostCameraHal::configureWindow() {
    if (!window) return 0;
    int minimum = 0;
    int status = window->get_min_undequeued_buffer_count(window, &minimum);
    if (!status) status = window->set_buffer_count(window, minimum + 2);
    if (!status) status = window->set_usage(window, GRALLOC_USAGE_SW_WRITE_OFTEN);
    if (!status) status = window->set_buffers_geometry(window, width, height, HAL_PIXEL_FORMAT_YV12);
    return status;
}

void HostCameraHal::present(const std::vector<unsigned char>& bytes, int64_t time) {
    if (!window || bytes.size() < static_cast<size_t>(width * height * 3 / 2)) return;
    const hw_module_t* hardware = nullptr;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &hardware)) return;
    auto* gralloc = reinterpret_cast<const gralloc_module_t*>(hardware);
    buffer_handle_t* buffer = nullptr;
    int stride = 0;
    if (window->dequeue_buffer(window, &buffer, &stride)) return;
    android_ycbcr planes{};
    int status = gralloc->lock_ycbcr(gralloc, *buffer, GRALLOC_USAGE_SW_WRITE_OFTEN,
                                    0, 0, width, height, &planes);
    if (!status) {
        auto* y = static_cast<unsigned char*>(planes.y);
        auto* cb = static_cast<unsigned char*>(planes.cb);
        auto* cr = static_cast<unsigned char*>(planes.cr);
        for (int row = 0; row < height; ++row)
            memcpy(y + row * planes.ystride, bytes.data() + row * width, width);
        for (int row = 0; row < height / 2; ++row) {
            const auto* source = bytes.data() + width * height + row * width;
            for (int col = 0; col < width / 2; ++col) {
                cr[row * planes.cstride + col * planes.chroma_step] = source[2 * col];
                cb[row * planes.cstride + col * planes.chroma_step] = source[2 * col + 1];
            }
        }
        status = gralloc->unlock(gralloc, *buffer);
    }
    if (!status) {
        window->set_timestamp(window, time);
        status = window->enqueue_buffer(window, buffer);
    }
    if (status) window->cancel_buffer(window, buffer);
}

void HostCameraHal::readEvents() {
    int32_t status;
    if (!events.get(&status) || status) return;
    while (true) {
        int32_t type, value, length;
        int64_t time;
        if (!events.get(&type) || !events.get(&value) || !events.get64(&time) || !events.get(&length) ||
                length < 0 || length > 32 * 1024 * 1024) return;
        std::vector<unsigned char> bytes(length);
        if (length && !events.getBytes(bytes.data(), bytes.size())) return;
        camera_notify_callback notice;
        camera_data_callback callback;
        camera_data_timestamp_callback timed;
        camera_request_memory memory;
        void* cookie;
        {
            std::lock_guard<std::mutex> guard(state);
            notice = notify; callback = data; timed = timestamp; memory = allocate; cookie = user;
            if (type == 1 && preview) present(bytes, time);
        }
        int flag = type == 1 ? CAMERA_MSG_PREVIEW_FRAME : type == 2 ? CAMERA_MSG_SHUTTER :
                type == 3 ? CAMERA_MSG_COMPRESSED_IMAGE : type == 4 ? CAMERA_MSG_FOCUS :
                type == 6 ? CAMERA_MSG_FOCUS_MOVE : CAMERA_MSG_ERROR;
        if ((messages & flag) && (type == 2 || type == 4 || type == 5 || type == 6)) {
            if (notice) notice(flag, value, 0, cookie);
        } else if ((messages & flag) && memory && callback && length) {
            camera_memory_t* image = memory(-1, length, 1, cookie);
            if (image && image->data) {
                memcpy(image->data, bytes.data(), length);
                callback(flag, image, 0, nullptr, cookie);
                image->release(image);
            }
        }
        if (type == 1 && recording && (messages & CAMERA_MSG_VIDEO_FRAME) && memory && timed) {
            camera_memory_t* image = memory(-1, length, 1, cookie);
            if (!image || !image->data) continue;
            memcpy(image->data, bytes.data(), length);
            {
                std::lock_guard<std::mutex> guard(state);
                if (videoFrames.size() >= 6) { image->release(image); continue; }
                videoFrames[image->data] = image;
            }
            timed(time, CAMERA_MSG_VIDEO_FRAME, image, 0, cookie);
        }
    }
}
