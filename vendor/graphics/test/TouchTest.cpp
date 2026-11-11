#include "TouchQueue.h"
#include "TouchState.h"
#include "SocketStream.h"
#include <cassert>
#include <cstdio>
#include <future>
#include <sys/socket.h>

using namespace touchwire;
template<class F> void rejects(F function) {
    bool failed = false;
    try { function(); } catch (const std::exception&) { failed = true; }
    assert(failed);
}
Event event(uint32_t seq, uint32_t action, std::initializer_list<int> ids, uint32_t index = 0) {
    Event result;
    result.sequence = seq; result.action = action; result.actionIndex = index;
    result.timeNanos = seq * 1000;
    for (int id : ids) result.pointers[result.count++] = {id, 20 + id, 30 + id, 0.5f};
    return result;
}
int main() {
    State state;
    state.apply(event(1, Down, {7}), 360, 640);
    state.apply(event(2, PointerDown, {7, 2}, 1), 360, 640);
    state.apply(event(3, Move, {2, 7}), 360, 640);
    state.apply(event(4, PointerUp, {2, 7}), 360, 640);
    assert(state.activeCount() == 1);
    state.apply(event(5, Up, {7}), 360, 640);
    assert(state.activeCount() == 0 && state.latest().sequence == 5);
    rejects([&] { state.apply(event(6, Move, {7}), 360, 640); });
    rejects([&] { state.apply(event(7, Down, {7}), 360, 640); });
    rejects([&] { validate(event(6, PointerDown, {7, 7}, 1), 360, 640); });
    rejects([&] { validate(event(6, Down, {32}), 360, 640); });
    auto invalid = event(6, Down, {7}); invalid.pointers[0].x = 360;
    rejects([&] { validate(invalid, 360, 640); });
    invalid = event(6, Down, {7}); invalid.pointers[0].pressure = NAN;
    rejects([&] { validate(invalid, 360, 640); });
    invalid = event(6, Down, {7}); invalid.timeNanos = 0;
    rejects([&] { state.apply(invalid, 360, 640); });
    state.apply(event(6, Down, {7}), 360, 640);
    state.apply(event(7, Cancel, {}), 360, 640);
    assert(state.activeCount() == 0);

    // No GL frames or render acknowledgments run during this burst.
    Queue queue;
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0);
    SocketStream sender(sockets[0]), receiver(sockets[1]);
    auto writer = std::async(std::launch::async, [&] {
        Event next;
        while (queue.next(next)) sender.writeFully(&next, sizeof(next));
    });
    for (uint32_t i = 1; i <= 200; ++i) queue.push(event(i, i % 2 ? Down : Up, {7}));
    queue.close();
    for (uint32_t i = 1; i <= 200; ++i) {
        Event received;
        receiver.readFully(&received, sizeof(received));
        assert(received.sequence == i && received.pointers[0].id == 7);
    }
    assert(writer.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    writer.get();
    Queue motion;
    motion.push(event(1, Down, {7}));
    for (uint32_t i = 2; i <= 201; ++i) motion.push(event(i, Move, {7}));
    motion.push(event(202, PointerDown, {7, 2}, 1));
    motion.push(event(203, Move, {7, 2}));
    motion.push(event(204, Move, {2, 7}));
    motion.push(event(205, PointerUp, {2, 7}));
    motion.push(event(206, Up, {7}));
    motion.close();
    Event next;
    for (uint32_t seq : {1u, 201u, 202u, 204u, 205u, 206u})
        assert(motion.next(next) && next.sequence == seq);
    assert(!motion.next(next));
    Queue waiting;
    auto waiter = std::async(std::launch::async, [&] { Event next; return waiting.next(next); });
    waiting.close();
    assert(!waiter.get());
    Queue full;
    for (uint32_t i = 1; i <= 512; ++i) full.push(event(i, Move, {7}));
    full.push(event(513, Up, {7}));
    Event stop; stop.running = 0; stop.sequence = 513;
    assert(full.finish(stop) && !full.finish(stop));
    rejects([&] { full.push(event(514, Up, {7})); });
    assert(full.next(next) && next.running && next.sequence == 512);
    assert(full.next(next) && next.running && next.action == Up && next.sequence == 513);
    assert(full.next(next) && !next.running && next.sequence == 513 && !full.next(next));
    std::puts("PASS multitouch transitions, pointer reorder, latest motion, cancel, ordered boundaries without GL, bounded queue, stop wakeup");
}
