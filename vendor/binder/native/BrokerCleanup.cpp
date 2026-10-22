#include "Broker.h"
#include <algorithm>
#include <unistd.h>

namespace a10binder {
void Broker::finishCall(uint64_t id) {
    auto call = calls.find(id);
    if (call == calls.end()) return;
    auto sender = connections.find(call->second.from);
    auto receiver = connections.find(call->second.receiver);
    if (sender != connections.end()) {
        auto& stack = sender->second.outgoing;
        stack.erase(std::remove(stack.begin(), stack.end(), id), stack.end());
        scheduleDispatch(sender->second.process);
    }
    if (receiver != connections.end()) {
        auto& stack = receiver->second.incoming;
        stack.erase(std::remove(stack.begin(), stack.end(), id), stack.end());
        scheduleDispatch(receiver->second.process);
    }
    calls.erase(call);
}

void Broker::closeConnection(uint64_t id) {
    auto it = connections.find(id);
    if (it == connections.end()) return;
    uint64_t process = it->second.process;
    if (it->second.control) { closeProcess(process); return; }
    if (it->second.registered && processes.find(process) != processes.end())
        --processes.at(process).registeredThreads;
    for (auto call = calls.begin(); call != calls.end();) {
        if (call->second.receiver == id) {
            event(call->second.from, BR_DEAD_REPLY);
            uint64_t finished = call->first;
            ++call;
            finishCall(finished);
        } else { if (call->second.from == id) call->second.from = 0; ++call; }
    }
    if (process && processes.find(process) != processes.end()) {
        while (!it->second.queue.empty()) {
            Packet& packet = it->second.queue.front();
            if (isIncomingTransaction(packet.header.command) || packet.header.command == BR_ACQUIRE ||
                packet.header.command == BR_INCREFS || packet.header.command == BR_RELEASE ||
                packet.header.command == BR_DECREFS || packet.header.command == BR_CLEAR_DEATH_NOTIFICATION_DONE)
                processEvent(process, std::move(packet));
            else if (packet.header.command == BR_REPLY) freeBuffer(process, packet.header.token);
            it->second.queue.pop_front();
        }
    }
    auto owner = processes.find(process);
    if (owner != processes.end()) {
        owner->second.threads.erase(id);
        scheduleDispatch(process);
    }
    close(it->second.fd); connections.erase(it);
}
void Broker::closeProcess(uint64_t id) {
    auto process = processes.find(id);
    if (process == processes.end()) return;
    for (auto& entry : nodes) if (entry.second.owner == id) {
        entry.second.alive = false;
        entry.second.pendingStrong = entry.second.pendingWeak = 0;
        entry.second.asyncQueue.clear();
        scheduleNodeReclaim(entry.second);
    }
    if (contextNode && nodes.at(contextNode).owner == id) contextNode = 0;
    for (auto& owner : processes) {
        if (owner.first == id) continue;
        for (Death& death : owner.second.deaths) if (nodes.at(death.node).owner == id && !death.notified) {
            death.notified = true;
            owner.second.notifiedCookies.insert(death.cookie);
            Packet packet; packet.header.kind = Event; packet.header.command = BR_DEAD_BINDER;
            packet.append(death.cookie); processEvent(owner.first, std::move(packet));
        }
    }
    for (auto it = calls.begin(); it != calls.end();) {
        if (it->second.targetProcess == id) {
            event(it->second.from, BR_DEAD_REPLY);
            uint64_t finished = it->first;
            ++it;
            finishCall(finished);
        } else {
            auto from = connections.find(it->second.from);
            if (from != connections.end() && from->second.process == id) it->second.from = 0;
            ++it;
        }
    }
    std::vector<uint64_t> ownedBuffers;
    for (const auto& entry : buffers) if (entry.second.owner == id) ownedBuffers.push_back(entry.first);
    for (uint64_t buffer : ownedBuffers) freeBuffer(id, buffer);
    for (const auto& entry : process->second.handles) {
        for (uint32_t n = 0; n < entry.second.strong; ++n) adjust(entry.second.node, true, -1);
        for (uint32_t n = 0; n < entry.second.weak; ++n) adjust(entry.second.node, false, -1);
        releaseNodeReference(entry.second.node);
    }
    for (const Death& death : process->second.deaths) releaseNodeReference(death.node);
    std::vector<uint64_t> threads;
    for (const auto& entry : connections) if (entry.second.process == id) threads.push_back(entry.first);
    for (uint64_t thread : threads) { close(connections.at(thread).fd); connections.erase(thread); }
    processes.erase(process);
    collectDeadNodes();
    ++counters.disconnected;
}
}
