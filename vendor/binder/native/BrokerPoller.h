#pragma once
#include <sys/epoll.h>
#include <unistd.h>
#include <stdint.h>

namespace a10binder {
class BrokerPoller {
public:
    BrokerPoller() : fd_(epoll_create1(EPOLL_CLOEXEC)) {}
    ~BrokerPoller() { if (fd_ >= 0) close(fd_); }
    BrokerPoller(const BrokerPoller&) = delete;
    BrokerPoller& operator=(const BrokerPoller&) = delete;
    bool valid() const { return fd_ >= 0; }
    bool add(int fd, uint64_t id) { return update(EPOLL_CTL_ADD, fd, id, false); }
    bool writable(int fd, uint64_t id, bool pending) { return update(EPOLL_CTL_MOD, fd, id, pending); }
    int wait(epoll_event* events, int capacity) { return epoll_wait(fd_, events, capacity, -1); }
private:
    int fd_;
    bool update(int operation, int fd, uint64_t id, bool pending) {
        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP | (pending ? EPOLLOUT : 0);
        event.data.u64 = id;
        return epoll_ctl(fd_, operation, fd, &event) == 0;
    }
};
}
