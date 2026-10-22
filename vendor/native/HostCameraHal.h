#pragma once
#include "HostDeviceTransport.h"
#include <hardware/camera.h>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct HostCameraHal {
    camera_device_t device{};
    HostDeviceSocket control{"camera", 0x41313043, 2};
    HostDeviceSocket events{"camera", 0x41313043, 3, true};
    std::mutex commands;
    std::mutex state;
    std::thread reader;
    std::atomic<int> messages{0};
    std::atomic<bool> preview{false}, recording{false};
    std::string parameters;
    int width = 640, height = 480;
    preview_stream_ops* window = nullptr;
    camera_notify_callback notify = nullptr;
    camera_data_callback data = nullptr;
    camera_data_timestamp_callback timestamp = nullptr;
    camera_request_memory allocate = nullptr;
    void* user = nullptr;
    std::map<const void*, camera_memory_t*> videoFrames;

    int open(int id);
    int command(int code, const std::string* text = nullptr, int value = 0);
    void updateSize();
    void readEvents();
    int configureWindow();
    void present(const std::vector<unsigned char>& bytes, int64_t time);
    void close();
};
