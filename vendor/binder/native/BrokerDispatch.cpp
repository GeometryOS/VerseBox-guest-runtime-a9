#include "Broker.h"
#include <errno.h>

namespace a10binder {
void Broker::scheduleDispatch(uint64_t id) {
    auto process = processes.find(id);
    if (process == processes.end() || process->second.dispatchPending) return;
    process->second.dispatchPending = true;
    dispatchReady_.push_back(id);
}

void Broker::scheduleOutput(uint64_t id) {
    auto connection = connections.find(id);
    if (connection == connections.end() || connection->second.outputPending) return;
    connection->second.outputPending = true;
    outputReady_.push_back(id);
}

void Broker::queueOutput(uint64_t id, Packet packet) {
    connections.at(id).output.push_back(std::move(packet));
    scheduleOutput(id);
}

void Broker::threadEvent(uint64_t id, Packet packet) {
    auto connection = connections.find(id);
    if (connection == connections.end()) return;
    connection->second.queue.push_back(std::move(packet));
    scheduleDispatch(connection->second.process);
}

void Broker::dispatch() {
    std::vector<uint64_t> ready;
    ready.swap(dispatchReady_);
    for (uint64_t processId : ready) {
        auto owner = processes.find(processId);
        if (owner == processes.end()) continue;
        Process& process = owner->second;
        bool pending = false;
        for (uint64_t id : process.threads) {
            Connection& connection = connections.at(id);
            if (connection.readCapacity) {
                std::deque<Packet>* queue = &connection.queue;
                // A synchronous caller only receives replies and directed callbacks.
                if (queue->empty() && connection.looper && connection.incoming.empty() && connection.outgoing.empty())
                    queue = &process.queue;
                if (!queue->empty()) {
#ifndef A9VM_PRIVATE_RUNTIME
                    if (queue == &process.queue && isIncomingTransaction(queue->front().header.command) &&
                            !process.spawnPending && process.registeredThreads < process.maxThreads) {
                        size_t available = 0;
                        for (uint64_t other : process.threads) {
                            const Connection& thread = connections.at(other);
                            if (thread.looper && thread.readCapacity && thread.incoming.empty() && thread.outgoing.empty())
                                ++available;
                        }
                        if (available == 1 && connection.incoming.empty()) {
                            event(id, BR_SPAWN_LOOPER);
                            queue = &connection.queue;
                            process.spawnPending = true;
                            ++counters.spawns;
                        }
                    }
#endif
                    Packet& packet = queue->front();
                    if (sizeof(uint32_t) + _IOC_SIZE(packet.header.command) > connection.readCapacity) {
                        acknowledge(id, EMSGSIZE);
                    } else {
                        if (isIncomingTransaction(packet.header.command) && packet.header.arg) {
                            auto call = calls.find(packet.header.arg);
                            if (call != calls.end()) {
                                call->second.receiver = id;
                                connection.incoming.push_back(call->first);
                            }
                        }
                        queueOutput(id, std::move(packet));
                        queue->pop_front();
                    }
                    connection.readCapacity = 0;
                }
            }
            pending |= !connection.queue.empty();
        }
        pending |= !process.queue.empty();
        if (pending && !process.notified) {
            Packet signal;
            signal.header.kind = Wakeup;
            queueOutput(process.control, std::move(signal));
        }
        process.notified = pending;
        process.dispatchPending = false;
    }
}
}
