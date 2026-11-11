#pragma once

#include "TouchProtocol.h"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace touchwire {
class Queue {
public:
    void push(const Event& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) throw std::runtime_error("Touch channel closed");
        // A full queue must not fault the caller: the host delivers this from the UI thread,
        // where an exception would crash the app. Shed the oldest motion sample instead,
        // keeping Down/Up so the guest's gesture state stays coherent.
        if (events_.size() >= 512) {
            auto oldest = std::find_if(events_.begin(), events_.end(),
                [](const Event& queued) { return queued.action == Move; });
            if (oldest != events_.end()) events_.erase(oldest);
            else events_.pop_front();
        }
        events_.push_back(event);
        changed_.notify_one();
    }
    bool next(Event& event) {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [&] { return closed_ || !events_.empty(); });
        if (events_.empty()) return false;
        event = events_.front();
        events_.pop_front();
        // Collapse superseded motion samples so the guest never replays stale positions it would
        // immediately overwrite. Down/Up/cancel boundaries are preserved.
        if (event.action == Move)
            while (!events_.empty() && events_.front().running && events_.front().action == Move) {
                event = events_.front();
                events_.pop_front();
            }
        return true;
    }
    bool finish(const Event& stop) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return false;
        events_.push_back(stop);
        closed_ = true;
        changed_.notify_all();
        return true;
    }
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        changed_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Event> events_;
    bool closed_ = false;
};
}
