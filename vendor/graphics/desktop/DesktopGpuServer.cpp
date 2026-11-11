#include "DesktopGpuServer.h"
#include "DesktopDecoder.h"
#include "DesktopProtocol.h"
#include "DesktopThread.h"
#include <cerrno>
#include <csignal>
#include <memory>
#include <system_error>
#include <poll.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

DesktopGpuServer::DesktopGpuServer(const std::string& path, const desktopdisplay::Mode& display)
    : path_(path), backend_(display.width, display.height, true, display.density, display.refreshHz) {
    listener_ = listenLocal(path_);
    if (chmod(path_.c_str(), 0600) < 0) {
        close(listener_);
        unlink(path_.c_str());
        throw std::runtime_error("Desktop GPU socket permissions");
    }
    wake_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wake_ < 0) {
        close(listener_);
        unlink(path_.c_str());
        throw std::runtime_error("Desktop GPU eventfd");
    }
    worker_ = std::thread([this] { run(); });
}

DesktopGpuServer::~DesktopGpuServer() {
    closing_ = true;
    uint64_t wake = 1;
    write(wake_, &wake, sizeof(wake));
    shutdown(listener_, SHUT_RDWR);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto client : clients_) shutdown(client.first, SHUT_RDWR);
    }
    worker_.join();
    for (auto& decoder : decoders_) if (decoder.thread.joinable()) decoder.thread.join();
    for (auto process : processes_) if (process.second.descriptor >= 0) close(process.second.descriptor);
    close(listener_);
    close(wake_);
    unlink(path_.c_str());
}

int64_t DesktopGpuServer::attach(DesktopPresenter::Window window) {
    auto generation = backend_.attach(std::move(window));
    uint64_t wake = 1;
    write(wake_, &wake, sizeof(wake));
    return generation;
}

void DesktopGpuServer::detach(int64_t generation) {
    backend_.detach(generation);
    uint64_t wake = 1;
    write(wake_, &wake, sizeof(wake));
}

void DesktopGpuServer::fail(const std::exception& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!closing_ && error_.empty()) error_ = error.what();
}
std::string DesktopGpuServer::error() {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

void DesktopGpuServer::reapDecoders() {
    for (auto decoder = decoders_.begin(); decoder != decoders_.end();) {
        if (!decoder->done->load(std::memory_order_acquire)) { ++decoder; continue; }
        if (decoder->thread.joinable()) decoder->thread.join();
        decoder = decoders_.erase(decoder);
        --decoderCount_;
    }
}

void DesktopGpuServer::reapProcesses() {
    for (auto process = processes_.begin(); process != processes_.end();) {
        if (!process->second.dead) { ++process; continue; }
        bool active = false;
        for (const auto& decoder : decoders_) active |= decoder.process == process->first;
        if (active) { ++process; continue; }
        // EGL handles are process-owned, not connection-owned: render threads
        // exchange contexts and mailbox images across separate GPU sockets.
        backend_.releaseProcess(process->first);
        if (process->second.descriptor >= 0) close(process->second.descriptor);
        process = processes_.erase(process);
    }
}

void DesktopGpuServer::run() {
    desktopthread::configure("a10-gpu-listen");
    try {
        while (!closing_) {
            std::vector<pollfd> waiting{{listener_, POLLIN, 0}, {wake_, POLLIN, 0}};
            std::vector<pid_t> watched;
            int timeout = -1;
            for (auto& process : processes_) {
                if (process.second.dead) continue;
                if (process.second.descriptor < 0) { timeout = 1000; continue; }
                waiting.push_back({process.second.descriptor, POLLIN, 0});
                watched.push_back(process.first);
            }
            int result = poll(waiting.data(), waiting.size(), timeout);
            if (result < 0 && errno == EINTR) continue;
            if (result < 0) throw std::runtime_error("Desktop GPU listen poll");
            if (closing_) continue;
            for (size_t index = 0; index < watched.size(); ++index)
                if (waiting[index + 2].revents & (POLLIN | POLLHUP | POLLERR))
                    processes_.at(watched[index]).dead = true;
            for (auto& process : processes_)
                if (process.second.descriptor < 0 && kill(process.first, 0) < 0 && errno == ESRCH)
                    process.second.dead = true;
            if (waiting[1].revents & POLLIN) {
                uint64_t wake;
                read(wake_, &wake, sizeof(wake));
                reapDecoders();
                backend_.refreshWindow();
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto client : clients_)
                    if (processes_.at(client.second).dead) shutdown(client.first, SHUT_RDWR);
            }
            reapProcesses();
            if (!(waiting[0].revents & POLLIN)) continue;
            int fd = accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC);
            if (fd < 0) throw std::runtime_error("Desktop GPU accept");
            ucred peer{};
            socklen_t bytes = sizeof(peer);
            if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &bytes) < 0 || peer.uid != getuid()) {
                close(fd);
                continue;
            }
            if (!processes_.count(peer.pid)) {
                const int descriptor = syscall(__NR_pidfd_open, peer.pid, 0);
                if (descriptor < 0 && errno == ESRCH) { close(fd); continue; }
                processes_.emplace(peer.pid, Process{descriptor});
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (closing_) { close(fd); break; }
                clients_.emplace(fd, peer.pid);
            }
            try {
                auto done = std::make_shared<std::atomic<bool>>(false);
                decoders_.emplace_back();
                decoders_.back().done = done;
                decoders_.back().process = peer.pid;
                ++decoderCount_;
                decoders_.back().thread = std::thread([this, fd, done, process = peer.pid,
                                                       processDescriptor = processes_.at(peer.pid).descriptor] {
                    client(fd, process, processDescriptor);
                    done->store(true, std::memory_order_release);
                    uint64_t wake = 1;
                    write(wake_, &wake, sizeof(wake));
                });
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                clients_.erase(fd);
                close(fd);
                throw;
            }
        }
    } catch (const std::exception& error) { fail(error); }
}

void DesktopGpuServer::client(int fd, pid_t process, int processDescriptor) {
    desktopthread::configure("a10-gpu-worker");
    GpuBackend::setProcess(process);
    // Remove the descriptor before SocketStream closes it so shutdown never sees a reused FD.
    std::unique_ptr<SocketStream> stream;
    try {
        stream.reset(new SocketStream(fd, 64 * 1024, 0, true));
        desktopgl::handshake(*stream, false);
        DesktopDecoder decoder(backend_);
        while (!closing_ && decoder.next(*stream)) { }
    } catch (const std::exception& error) {
        pollfd ended{processDescriptor, POLLIN, 0};
        const bool dead = processDescriptor >= 0 ? poll(&ended, 1, 0) > 0 :
            kill(process, 0) < 0 && errno == ESRCH;
        const auto* transport = dynamic_cast<const std::system_error*>(&error);
        const int code = transport ? transport->code().value() : 0;
        // The socket can reset before pidfd becomes readable during app shutdown.
        const bool disconnected = transport && transport->code().category() == std::generic_category() &&
            (code == ECONNRESET || code == EPIPE || code == ENOTCONN);
        if (!dead && !disconnected) fail(error);
    }
    backend_.makeCurrent(0, 0, 0);
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.erase(fd);
    if (!stream) close(fd);
}
