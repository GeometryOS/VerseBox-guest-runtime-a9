#pragma once

#include "SocketStream.h"
#include "WindowEgl.h"
#include "WindowProtocol.h"
#include "TouchQueue.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

class WindowRenderer {
public:
    enum Metric {
        Ready, Frames, Swaps, InputSequence, EchoSequence, PresentedFrame,
        RequestedGeneration, BoundGeneration, InputNanos, SubmitNanos, InputSubmitNanos,
        Pid, Uid, Done, Dropped, Euid, PresentedGeneration, MetricCount
    };
    explicit WindowRenderer(const std::string& directory);
    ~WindowRenderer();
    int64_t attach(WindowHandle window);
    void detach(int64_t generation);
    uint32_t tap(int x, int y);
    uint32_t touch(touchwire::Event event);
    bool requestExit();
    std::array<int64_t, MetricCount> metrics();
    std::vector<int64_t> intervals();
    void resetTimings();
    std::string error();
    std::string description();
private:
    void run();
    int acceptPeer(int listener);
    void updateWindow(WindowEgl& target);
    void finishFrame(WindowEgl& target);
    void sendInput();
    std::mutex mutex_;
    std::condition_variable stopWritten_;
    std::atomic<bool> closing_{false};
    bool exitRequested_ = false;
    bool stopWriteSucceeded_ = false;
    WindowHandle requestedWindow_;
    windowprobe::Input input_;
    touchwire::Queue touches_;
    uint32_t previousFrame_ = 0;
    uint32_t submittedInput_ = 0;
    int64_t lastSwapNanos_ = 0, lastSwapGeneration_ = 0;
    std::array<int64_t, MetricCount> metrics_{};
    std::vector<int64_t> intervals_;
    std::string error_, description_, glPath_, inputPath_;
    std::unique_ptr<SocketStream> glListener_, inputListener_, gl_, inputStream_;
    std::thread worker_, inputWorker_;
};
