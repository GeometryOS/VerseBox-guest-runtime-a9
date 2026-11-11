#include "DesktopInputChannel.h"
#include "DesktopDisplay.h"
#include "DesktopThread.h"
#include <chrono>
#include <memory>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

DesktopInputChannel::DesktopInputChannel(const std::string& path, int width, int height)
    : path_(path), width_(width), height_(height) {
    listener_ = listenLocal(path_);
    if (chmod(path_.c_str(), 0600) < 0) {
        close(listener_);
        unlink(path_.c_str());
        throw std::runtime_error("Desktop input socket permissions");
    }
    worker_ = std::thread([this] { run(); });
}
DesktopInputChannel::~DesktopInputChannel() {
    closing_ = true;
    queue_.close();
    shutdown(listener_, SHUT_RDWR);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (client_ >= 0) shutdown(client_, SHUT_RDWR);
    }
    worker_.join();
    close(listener_);
    unlink(path_.c_str());
}
uint32_t DesktopInputChannel::submit(touchwire::Event event) {
    touchwire::validate(event, width_, height_);
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_ || closing_) return 0;
    event.sequence = ++sequence_;
    queue_.push(event);
    time_ = event.timeNanos;
    return sequence_;
}
void DesktopInputChannel::finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_) return;
    finished_ = true;
    touchwire::Event event;
    event.running = 0;
    event.sequence = sequence_;
    queue_.finish(event);
}
std::array<int64_t, 3> DesktopInputChannel::state() {
    std::lock_guard<std::mutex> lock(mutex_);
    return {sequence_, accepted_, time_};
}
std::string DesktopInputChannel::error() {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}
void DesktopInputChannel::run() {
    desktopthread::configure("a10-input", -4);
    std::unique_ptr<SocketStream> stream;
    try {
        int fd = accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) throw std::runtime_error("Desktop input accept");
        stream.reset(new SocketStream(fd, 4096, 0));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            client_ = fd;
            if (closing_) shutdown(fd, SHUT_RDWR);
        }
        ucred peer{};
        socklen_t bytes = sizeof(peer);
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &bytes) < 0 || peer.uid != getuid())
            throw std::runtime_error("Desktop input peer identity");
        touchwire::Event event;
        while (!closing_ && queue_.next(event)) {
            stream->writeFully(&event, sizeof(event));
            if (!event.running) break;
            uint32_t reply[3]{};
            stream->readFully(reply, sizeof(reply));
            if (reply[0] != 0x4131494e || reply[1] != event.sequence || reply[2] > 1)
                throw std::runtime_error("Desktop input acknowledgement protocol");
            std::lock_guard<std::mutex> lock(mutex_);
            if (reply[2]) accepted_ = event.sequence;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        client_ = -1;
    } catch (const std::exception& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        client_ = -1;
        if (!closing_) error_ = error.what();
    }
}
