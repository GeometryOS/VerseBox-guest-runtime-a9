#pragma once
#include "GpuBackend.h"
#include "DesktopDisplay.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <vector>

class DesktopGpuServer {
public:
    /** @param display 本次会话的显示几何：帧缓冲按它建，rcGetFBParam 按它上报。 */
    explicit DesktopGpuServer(const std::string& path, const desktopdisplay::Mode& display);
    ~DesktopGpuServer();
    GpuBackend& backend() { return backend_; }
    int64_t attach(DesktopPresenter::Window window);
    void detach(int64_t generation);
    std::string error();
    size_t workerCount() const { return decoderCount_.load(); }
private:
    void run();
    void client(int fd, pid_t process, int processDescriptor);
    void reapDecoders();
    void reapProcesses();
    void fail(const std::exception& error);
    // 尺寸与 DPI/刷新率来自**本次会话**的显示模式（不是进程全局值：同一进程里可以多台并存）。
    // 由构造函数初始化——不能用默认成员初始化器：那里拿不到构造参数。
    GpuBackend backend_;
    std::string path_, error_;
    int listener_ = -1;
    int wake_ = -1;
    std::atomic<bool> closing_{false};
    std::mutex mutex_;
    std::map<int, pid_t> clients_;
    struct Process { int descriptor; bool dead = false; };
    std::map<pid_t, Process> processes_;
    std::thread worker_;
    struct DecoderWorker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
        pid_t process = 0;
    };
    std::vector<DecoderWorker> decoders_;
    std::atomic<size_t> decoderCount_{0};
};
