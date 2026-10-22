#include "Broker.h"
#include <errno.h>
#include <algorithm>

namespace a10binder {
uint64_t Broker::resolve(uint64_t process, uint32_t handle) {
    if (handle == 0) return contextNode;
    auto it = processes.at(process).handles.find(handle);
    return it == processes.at(process).handles.end() ? 0 : it->second.node;
}
uint64_t Broker::exportNode(uint64_t connection, const flat_binder_object& object) {
    uint64_t owner = connections.at(connection).process;
    for (const auto& entry : nodes) {
        if (entry.second.owner == owner && entry.second.ptr == object.binder) {
            if (entry.second.cookie != object.cookie) return 0;
            return entry.first;
        }
    }
    uint64_t id = nextId++;
    nodes.emplace(id, Node{id, owner, connection, object.binder, object.cookie, object.flags});
    scheduleNodeReclaim(nodes.at(id));
    return id;
}
uint32_t Broker::importHandle(uint64_t process, uint64_t node) {
    if (node == contextNode) return 0;
    Process& owner = processes.at(process);
    for (const auto& entry : owner.handles) if (entry.second.node == node) return entry.first;
    uint32_t handle = owner.nextHandle++;
    owner.handles.emplace(handle, Handle{node});
    ++nodes.at(node).containerReferences;
    return handle;
}
void Broker::referenceEvent(Node& node, uint32_t code) {
    if (!node.alive || !node.ptr) return;
    if (code == BR_ACQUIRE) ++node.pendingStrong;
    if (code == BR_INCREFS) ++node.pendingWeak;
    binder_ptr_cookie value{node.ptr, node.cookie};
    auto thread = connections.find(node.exporter);
    if (thread != connections.end()) event(thread->first, code, &value, sizeof(value));
    else {
        Packet packet;
        packet.header.kind = Event; packet.header.command = code;
        packet.append(value); processEvent(node.owner, std::move(packet));
    }
}
void Broker::updateReferences(Node& node) {
    bool strong = node.strong || node.pendingStrong;
    bool weak = strong || node.weak || node.pendingWeak;
    if (weak && !node.heldWeak) referenceEvent(node, BR_INCREFS);
    if (strong && !node.heldStrong) referenceEvent(node, BR_ACQUIRE);
    if (!strong && node.heldStrong) referenceEvent(node, BR_RELEASE);
    if (!weak && node.heldWeak) referenceEvent(node, BR_DECREFS);
    node.heldStrong = strong;
    node.heldWeak = weak;
}
void Broker::adjust(uint64_t id, bool strong, int change) {
    Node& node = nodes.at(id);
    node.weak += change;
    if (strong) node.strong += change;
    if (change < 0) scheduleNodeReclaim(node);
    updateReferences(node);
}
void Broker::trimHandle(uint64_t process, uint32_t handle) {
    Process& owner = processes.at(process);
    auto it = owner.handles.find(handle);
    if (it == owner.handles.end() || it->second.strong || it->second.weak || it->second.temporary) return;
    for (const Death& death : owner.deaths) if (death.handle == handle) return;
    releaseNodeReference(it->second.node);
    owner.handles.erase(it);
}
int Broker::translate(uint64_t source, uint64_t destination, Packet* packet, Buffer* buffer) {
    binder_transaction_data transaction;
    if (!transactionLayout(*packet, &transaction)) return errno;
    size_t offsets = sizeof(transaction) + aligned(transaction.data_size);
    for (size_t at = 0; at < transaction.offsets_size; at += sizeof(binder_size_t)) {
        binder_size_t offset;
        flat_binder_object object;
        packet->get(offsets + at, &offset);
        packet->get(sizeof(transaction) + offset, &object);
        if (object.hdr.type == BINDER_TYPE_FD || object.hdr.type == BINDER_TYPE_PTR ||
            object.hdr.type == BINDER_TYPE_FDA) continue;
        bool strong = object.hdr.type == BINDER_TYPE_BINDER || object.hdr.type == BINDER_TYPE_HANDLE;
        bool local = object.hdr.type == BINDER_TYPE_BINDER || object.hdr.type == BINDER_TYPE_WEAK_BINDER;
        if (local && !object.binder && !object.cookie) continue;
        if (local && (!object.binder || !object.cookie)) return ENOTSUP;
        uint64_t id = local ? exportNode(source, object) : resolve(connections.at(source).process, object.handle);
        if (!id || nodes.find(id) == nodes.end()) return EINVAL;
        Node& node = nodes.at(id);
        uint32_t handle = 0;
        if (node.owner == destination) {
            object.hdr.type = strong ? BINDER_TYPE_BINDER : BINDER_TYPE_WEAK_BINDER;
            object.binder = node.ptr; object.cookie = node.cookie;
        } else {
            handle = importHandle(destination, id);
            object.hdr.type = strong ? BINDER_TYPE_HANDLE : BINDER_TYPE_WEAK_HANDLE;
            object.binder = 0; object.handle = handle; object.cookie = 0;
            if (handle) ++processes.at(destination).handles.at(handle).temporary;
        }
        object.flags = node.flags;
        adjust(id, strong, 1);
        buffer->references.push_back({id, handle, strong});
        memcpy(packet->data.data() + sizeof(transaction) + offset, &object, sizeof(object));
    }
    return 0;
}
int Broker::freeBuffer(uint64_t process, uint64_t id) {
    auto it = buffers.find(id);
    if (it == buffers.end() || it->second.owner != process) return EINVAL;
    Buffer buffer = std::move(it->second);
    buffers.erase(it);
    processes.at(process).bufferBytes -= buffer.bytes;
    for (const Reference& reference : buffer.references) {
        adjust(reference.node, reference.strong, -1);
        if (reference.handle) {
            --processes.at(process).handles.at(reference.handle).temporary;
            trimHandle(process, reference.handle);
        }
    }
    if (buffer.asyncNode && nodes.find(buffer.asyncNode) != nodes.end()) {
        Node& node = nodes.at(buffer.asyncNode);
        node.asyncBusy = false;
        scheduleNodeReclaim(node);
        if (!node.asyncQueue.empty()) {
            processEvent(node.owner, std::move(node.asyncQueue.front()));
            node.asyncQueue.pop_front(); node.asyncBusy = true;
        }
    }
    ++counters.freed;
    return 0;
}
int Broker::command(uint64_t id, Packet packet) {
    Connection& connection = connections.at(id);
    Process& process = processes.at(connection.process);
    uint32_t code = packet.header.command;
    if (code == BC_TRANSACTION || code == BC_REPLY) return transact(id, std::move(packet));
    if (!packet.fds.empty()) return EINVAL;
    if (code == BC_FREE_BUFFER) return packet.data.empty() ? freeBuffer(process.id, packet.header.token) : EINVAL;
    if (packet.data.size() != _IOC_SIZE(code)) return EINVAL;
    switch (code) {
        case BC_ENTER_LOOPER: case BC_REGISTER_LOOPER:
            if (connection.looper) return EINVAL;
            if (code == BC_REGISTER_LOOPER) {
                if (!process.spawnPending || process.registeredThreads >= process.maxThreads) return EINVAL;
                process.spawnPending = false;
                ++process.registeredThreads;
                connection.registered = true;
            }
            connection.looper = true;
            return 0;
        case BC_EXIT_LOOPER:
            if (connection.registered) --process.registeredThreads;
            connection.looper = connection.registered = false;
            return 0;
        case BC_INCREFS_DONE: case BC_ACQUIRE_DONE: {
            binder_ptr_cookie value;
            packet.get(0, &value);
            for (auto& entry : nodes) if (entry.second.owner == process.id &&
                entry.second.ptr == value.ptr && entry.second.cookie == value.cookie) {
                uint32_t& pending = code == BC_INCREFS_DONE ? entry.second.pendingWeak : entry.second.pendingStrong;
                if (!pending) return EINVAL;
                --pending;
                scheduleNodeReclaim(entry.second);
                updateReferences(entry.second);
                return 0;
            }
            return EINVAL;
        }
        case BC_INCREFS: case BC_ACQUIRE: case BC_RELEASE: case BC_DECREFS: {
            uint32_t handle; packet.get(0, &handle);
            uint64_t node = resolve(process.id, handle);
            if (!node) return EINVAL;
            if (!handle) return 0;
            Handle& reference = process.handles.at(handle);
            bool strong = code == BC_ACQUIRE || code == BC_RELEASE;
            uint32_t& count = strong ? reference.strong : reference.weak;
            int change = code == BC_ACQUIRE || code == BC_INCREFS ? 1 : -1;
            if (change < 0 && !count) return EINVAL;
            count += change; adjust(node, strong, change); trimHandle(process.id, handle);
            return 0;
        }
        case BC_REQUEST_DEATH_NOTIFICATION: case BC_CLEAR_DEATH_NOTIFICATION: {
            binder_handle_cookie value; packet.get(0, &value);
            uint64_t node = resolve(process.id, value.handle);
            if (!node) return EINVAL;
            auto it = std::find_if(process.deaths.begin(), process.deaths.end(), [&](const Death& item) {
                return item.handle == value.handle && item.cookie == value.cookie;
            });
            if (code == BC_CLEAR_DEATH_NOTIFICATION) {
                if (it == process.deaths.end()) return EINVAL;
                releaseNodeReference(it->node);
                process.deaths.erase(it);
                event(id, BR_CLEAR_DEATH_NOTIFICATION_DONE, &value.cookie, sizeof(value.cookie));
                trimHandle(process.id, value.handle);
            } else {
                if (it != process.deaths.end()) return EEXIST;
                bool dead = !nodes.at(node).alive;
                process.deaths.push_back({value.handle, node, value.cookie, dead});
                ++nodes.at(node).containerReferences;
                if (dead) {
                    Packet death; death.header.kind = Event; death.header.command = BR_DEAD_BINDER;
                    death.append(value.cookie); processEvent(process.id, std::move(death));
                    process.notifiedCookies.insert(value.cookie);
                }
            }
            return 0;
        }
        case BC_DEAD_BINDER_DONE: {
            uint64_t cookie; packet.get(0, &cookie);
            return process.notifiedCookies.erase(cookie) ? 0 : EINVAL;
        }
        default: return ENOTSUP;
    }
}
}
