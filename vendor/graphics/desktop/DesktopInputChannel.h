#pragma once
#include "SocketStream.h"
#include "TouchQueue.h"
#include "DesktopDisplay.h"
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class DesktopInputChannel {
public:
    /** @param width,height 本次会话的客体像素尺寸：触摸坐标按它校验。 */
    DesktopInputChannel(const std::string& path, int width, int height);
    ~DesktopInputChannel();
    uint32_t submit(touchwire::Event event);
    void finish();
    std::array<int64_t, 3> state();
    std::string error();
private:
    void run();
    std::string path_, error_;
    std::mutex mutex_;
    std::atomic<bool> closing_{false};
    bool finished_ = false;
    int listener_ = -1, client_ = -1;
    uint32_t sequence_ = 0, accepted_ = 0;
    int64_t time_ = 0;
    int width_ = desktopdisplay::width, height_ = desktopdisplay::height;
    touchwire::Queue queue_;
    std::thread worker_;
};
