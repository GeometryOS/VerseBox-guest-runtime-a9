#pragma once

#include "TouchProtocol.h"
#include <set>

namespace touchwire {
class State {
public:
    void apply(const Event& event, int width, int height) {
        validate(event, width, height);
        if (!event.running) return;
        if (event.sequence != latest_.sequence + 1 || event.timeNanos < latest_.timeNanos)
            throw std::runtime_error("Touch sequence/time mismatch");
        std::set<int> next;
        for (uint32_t i = 0; i < event.count; ++i) next.insert(event.pointers[i].id);
        const int changed = event.count ? event.pointers[event.actionIndex].id : -1;
        switch (event.action) {
            case Down:
                if (!active_.empty()) fail();
                break;
            case PointerDown: {
                auto previous = next;
                previous.erase(changed);
                if (previous != active_ || active_.count(changed)) fail();
                break;
            }
            case Move:
            case Up:
            case PointerUp:
                if (next != active_) fail();
                break;
            case Cancel:
                next.clear();
                break;
        }
        if (event.count) last_ = event.pointers[event.actionIndex];
        if (event.action == Up || event.action == PointerUp) next.erase(changed);
        active_ = std::move(next);
        latest_ = event;
    }
    const Event& latest() const { return latest_; }
    const Pointer& last() const { return last_; }
    size_t activeCount() const { return active_.size(); }
private:
    [[noreturn]] static void fail() { throw std::runtime_error("Touch transition mismatch"); }
    std::set<int> active_;
    Event latest_;
    Pointer last_{0, 180, 320, 0};
};
}
