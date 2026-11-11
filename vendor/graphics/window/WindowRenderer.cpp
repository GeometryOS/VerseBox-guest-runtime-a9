#include "WindowRenderer.h"
#include "ProbeDecoder.h"
#include "ProbeProtocol.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
int64_t nowNanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

WindowRenderer::WindowRenderer(const std::string& directory)
    : glPath_(directory + "/gles.sock"), inputPath_(directory + "/input.sock") {
    glListener_ = std::make_unique<SocketStream>(listenLocal(glPath_));
    inputListener_ = std::make_unique<SocketStream>(listenLocal(inputPath_));
    metrics_[Pid] = getpid(); metrics_[Uid] = getuid(); metrics_[Euid] = geteuid();
    worker_ = std::thread([this] { run(); });
}

WindowRenderer::~WindowRenderer() {
    closing_ = true;
    touches_.close();
    stopWritten_.notify_all();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* stream : {glListener_.get(), inputListener_.get(), gl_.get(), inputStream_.get()}) {
            if (stream) shutdown(stream->fd(), SHUT_RDWR);
        }
    }
    if (worker_.joinable()) worker_.join();
    if (inputWorker_.joinable()) inputWorker_.join();
    unlink(glPath_.c_str()); unlink(inputPath_.c_str());
}

int64_t WindowRenderer::attach(WindowHandle window) {
    std::lock_guard<std::mutex> lock(mutex_);
    requestedWindow_ = std::move(window);
    return ++metrics_[RequestedGeneration];
}

void WindowRenderer::detach(int64_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (metrics_[RequestedGeneration] != generation) return;
    requestedWindow_.reset();
    ++metrics_[RequestedGeneration];
}

uint32_t WindowRenderer::tap(int x, int y) {
    touchwire::Event event;
    event.action = touchwire::Down; event.count = 1;
    event.timeNanos = nowNanos();
    event.pointers[0] = {0, std::clamp(x, 0, windowprobe::width - 1),
                        std::clamp(y, 0, windowprobe::height - 1), 1.f};
    touch(event);
    event.action = touchwire::Up;
    return touch(event);
}

uint32_t WindowRenderer::touch(touchwire::Event event) {
    touchwire::validate(event, windowprobe::width, windowprobe::height);
    std::lock_guard<std::mutex> lock(mutex_);
    if (exitRequested_ || closing_) return 0;
    event.sequence = input_.sequence + 1;
    touches_.push(event);
    if (event.count) {
        input_.x = event.pointers[event.actionIndex].x;
        input_.y = event.pointers[event.actionIndex].y;
    }
    metrics_[InputNanos] = nowNanos();
    metrics_[InputSequence] = ++input_.sequence;
    return input_.sequence;
}

bool WindowRenderer::requestExit() {
    touchwire::Event stop;
    std::unique_lock<std::mutex> lock(mutex_);
    if (!exitRequested_) {
        exitRequested_ = true;
        stop.running = 0;
        stop.sequence = input_.sequence;
        if (!inputStream_ || !touches_.finish(stop)) return false;
    }
    stopWritten_.wait_for(lock, std::chrono::seconds(2), [&] {
        return stopWriteSucceeded_ || closing_ || !error_.empty();
    });
    return stopWriteSucceeded_;
}

std::array<int64_t, WindowRenderer::MetricCount> WindowRenderer::metrics() {
    std::lock_guard<std::mutex> lock(mutex_); return metrics_;
}
std::vector<int64_t> WindowRenderer::intervals() {
    std::lock_guard<std::mutex> lock(mutex_); return intervals_;
}
void WindowRenderer::resetTimings() {
    std::lock_guard<std::mutex> lock(mutex_); intervals_.clear(); lastSwapNanos_ = 0;
}
std::string WindowRenderer::error() {
    std::lock_guard<std::mutex> lock(mutex_); return error_;
}
std::string WindowRenderer::description() {
    std::lock_guard<std::mutex> lock(mutex_); return description_;
}

int WindowRenderer::acceptPeer(int listener) {
    while (!closing_) {
        pollfd waiting{listener, POLLIN, 0};
        int result = poll(&waiting, 1, 100);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) throw std::runtime_error("Window accept poll failed");
        if (!result) continue;
        int fd = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) throw std::runtime_error("Window accept failed");
        return fd;
    }
    throw std::runtime_error("Window listener stopped");
}

void WindowRenderer::updateWindow(WindowEgl& target) {
    WindowHandle window;
    int64_t generation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation = metrics_[RequestedGeneration];
        if (generation == metrics_[BoundGeneration]) return;
        window = requestedWindow_;
    }
    target.bind(std::move(window));
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_[BoundGeneration] = generation;
}

void WindowRenderer::sendInput() {
    try {
        touchwire::Event next;
        while (touches_.next(next)) {
            if (closing_) return;
            inputStream_->writeFully(&next, sizeof(next));
            if (!next.running) {
                std::lock_guard<std::mutex> lock(mutex_);
                stopWriteSucceeded_ = true;
                stopWritten_.notify_all();
                return;
            }
        }
    } catch (const std::exception& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!closing_) error_ = error.what();
        stopWritten_.notify_all();
    }
}

void WindowRenderer::finishFrame(WindowEgl& target) {
    windowprobe::Frame frame;
    inputStream_->readFully(&frame, sizeof(frame));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        windowprobe::validate(frame, previousFrame_, metrics_[EchoSequence], input_.sequence);
    }
    previousFrame_ = frame.sequence;
    bool unchanged;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        unchanged = metrics_[RequestedGeneration] == metrics_[BoundGeneration];
    }
    bool swapped = unchanged && target.swap();
    const auto now = nowNanos();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_[Frames] = frame.sequence;
        metrics_[EchoSequence] = frame.inputSequence;
        if (swapped) {
            const auto generation = metrics_[BoundGeneration];
            if (lastSwapNanos_ && lastSwapGeneration_ == generation && intervals_.size() < 4096) {
                intervals_.push_back(now - lastSwapNanos_);
            }
            lastSwapNanos_ = now; lastSwapGeneration_ = generation;
            ++metrics_[Swaps]; metrics_[PresentedFrame] = frame.sequence;
            metrics_[PresentedGeneration] = generation; metrics_[SubmitNanos] = now;
            if (frame.inputSequence != submittedInput_) {
                metrics_[InputSubmitNanos] = now;
                submittedInput_ = frame.inputSequence;
            }
        } else {
            ++metrics_[Dropped];
        }
    }
    updateWindow(target);
}

void WindowRenderer::run() {
    try {
        WindowEgl target;
        target.initialize();
        updateWindow(target);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            description_ = target.description(); metrics_[Ready] = 1;
        }
        auto gl = std::make_unique<SocketStream>(acceptPeer(glListener_->fd()));
        {
            std::lock_guard<std::mutex> lock(mutex_); gl_ = std::move(gl);
        }
        probe::handshake(*gl_, false);
        auto control = std::make_unique<SocketStream>(acceptPeer(inputListener_->fd()));
        {
            std::lock_guard<std::mutex> lock(mutex_); inputStream_ = std::move(control);
        }
        probe::handshake(*inputStream_, false);
        inputWorker_ = std::thread([this] { sendInput(); });
        ProbeDecoder decoder([&] { finishFrame(target); });
        while (!closing_ && decoder.next(*gl_)) {}
    } catch (const std::exception& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!closing_) error_ = error.what();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_[Done] = 1;
}
