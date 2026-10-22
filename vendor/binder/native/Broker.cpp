#include "Broker.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>

namespace a10binder {
static volatile sig_atomic_t stopped;
static void stop(int) { stopped = 1; }
void Broker::acknowledge(uint64_t id, int status, uint64_t token, uint64_t capabilities) {
    if (status) {
        char line[120];
        int length = snprintf(line, sizeof(line), "a10binder broker ack status=%d token=%llu\n", status,
                              static_cast<unsigned long long>(token));
        if (length > 0) write(2, line, static_cast<size_t>(length));
    }
    Packet packet;
    packet.header.kind = Ack;
    packet.header.status = status;
    packet.header.token = token;
    packet.header.arg = capabilities;
    queueOutput(id, std::move(packet));
}
void Broker::event(uint64_t id, uint32_t code, const void* data, size_t size) {
    auto it = connections.find(id);
    if (it == connections.end()) return;
    Packet packet;
    packet.header.kind = Event;
    packet.header.command = code;
    packet.append(data, size);
    threadEvent(id, std::move(packet));
}
void Broker::processEvent(uint64_t id, Packet packet) {
    auto it = processes.find(id);
    if (it != processes.end()) {
        it->second.queue.push_back(std::move(packet));
        scheduleDispatch(id);
    }
}
void Broker::acceptConnection() {
    int fd = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0) return;
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 ||
        credentials.uid != getuid()) { close(fd); return; }
    uint64_t id = nextId++;
    if (!poller.add(fd, id)) { close(fd); return; }
    connections.emplace(id, Connection{fd, credentials});
}
void Broker::request(uint64_t id, Packet packet) {
    Connection& connection = connections.at(id);
    if (connection.process) scheduleDispatch(connection.process);
    if (packet.header.kind == Inspect && !connection.process && packet.data.empty() && packet.fds.empty()) {
        if (getenv("A10VM_DEBUG_PENDING")) {
            for (const auto& item : calls) {
                const Call& call = item.second;
                auto sender = connections.find(call.from);
                auto receiver = connections.find(call.receiver);
                fprintf(stderr, "[DEBUG-desktop-pending] call=%llu from=%d/%llu to=%d/%llu parent=%llu\n",
                    (unsigned long long)item.first, sender == connections.end() ? 0 : sender->second.peer.pid,
                    sender == connections.end() ? 0 : (unsigned long long)sender->second.tid,
                    processes.at(call.targetProcess).pid, receiver == connections.end() ? 0 : (unsigned long long)receiver->second.tid,
                    (unsigned long long)call.parent);
            }
            for (const auto& item : connections) {
                const Connection& client = item.second;
                fprintf(stderr, "[DEBUG-desktop-pending] thread=%d/%llu read=%zu queued=%zu output=%zu looper=%d\n",
                    client.peer.pid, (unsigned long long)client.tid, client.readCapacity, client.queue.size(), client.output.size(), client.looper);
            }
        }
        Packet reply;
        reply.header.kind = Ack;
        reply.append(statistics());
        queueOutput(id, std::move(reply));
        return;
    }
    if (packet.header.kind == Hello && !connection.process && packet.data.empty() && packet.fds.empty()) {
        if (packet.header.token == 0 && packet.header.arg == 0) {
            // Each device open owns a Binder context, including repeated opens by one PID.
            uint64_t process = nextId++;
            processes.emplace(process, Process{process, id, connection.peer.pid, connection.peer.uid});
            processes.at(process).guestUid = packet.header.command ? packet.header.command - 1 : connection.peer.uid;
            connection.process = process;
            connection.control = true;
            acknowledge(id, 0, process, kCommandRead);
        } else {
            auto process = processes.find(packet.header.token);
            char path[80];
            snprintf(path, sizeof(path), "/proc/%d/task/%llu", connection.peer.pid,
                     static_cast<unsigned long long>(packet.header.arg));
            if (process == processes.end() || process->second.pid != connection.peer.pid ||
                process->second.uid != connection.peer.uid || access(path, F_OK) != 0) {
                acknowledge(id, EPERM);
                return;
            }
            for (uint64_t thread : process->second.threads) {
                if (connections.at(thread).tid == packet.header.arg) {
                    acknowledge(id, EEXIST);
                    return;
                }
            }
            connection.process = process->first;
            connection.tid = packet.header.arg;
            process->second.threads.insert(id);
            scheduleDispatch(connection.process);
            acknowledge(id, 0, connection.process, kCommandRead);
        }
        return;
    }
    if (connection.process && connection.control && packet.header.kind == Rearm &&
        packet.data.empty() && packet.fds.empty() && !packet.header.command &&
        !packet.header.status && !packet.header.token && !packet.header.arg) {
        for (auto at = connection.output.begin(); at != connection.output.end();) {
            if (at->header.kind == Wakeup) at = connection.output.erase(at);
            else ++at;
        }
        // ACK orders the old notifications before a fresh level-triggered readiness signal.
        acknowledge(id);
        processes.at(connection.process).notified = false;
        return;
    }
    if (!connection.process || connection.control) { acknowledge(id, EPROTO); return; }
    switch (packet.header.kind) {
        case Command: acknowledge(id, command(id, std::move(packet))); break;
        case CommandRead: {
            uint64_t capacity = packet.header.arg;
            if (!capacity || connection.readCapacity) { acknowledge(id, EINVAL); break; }
            int status = command(id, std::move(packet));
            acknowledge(id, status);
            if (!status) connection.readCapacity = capacity;
            break;
        }
        case Read:
            if (connection.readCapacity || !packet.data.empty() || !packet.fds.empty()) acknowledge(id, EINVAL);
            else connection.readCapacity = packet.header.arg;
            break;
        case Context:
            if (contextNode) { acknowledge(id, EBUSY); break; }
            contextNode = nextId++;
            nodes.emplace(contextNode, Node{contextNode, connection.process, id, 0, 0, FLAT_BINDER_FLAG_ACCEPTS_FDS});
            scheduleNodeReclaim(nodes.at(contextNode));
            acknowledge(id);
            break;
        case MaxThreads:
            if (packet.header.arg > 64) acknowledge(id, EINVAL);
            else { processes.at(connection.process).maxThreads = packet.header.arg; acknowledge(id); }
            break;
        case Exit:
            connection.looper = false;
            acknowledge(id);
            break;
        default: acknowledge(id, ENOTSUP); break;
    }
    collectDeadNodes();
}
Stats Broker::statistics() {
    Stats result = counters;
    result.processes = processes.size();
    result.threads = result.handles = result.deaths = result.queuedFds = 0;
    result.pendingReferences = result.registeredLoopers = 0;
    result.nodes = nodes.size(); result.buffers = buffers.size(); result.calls = calls.size();
    for (const auto& entry : connections) {
        result.threads += entry.second.tid != 0;
        for (const Packet& p : entry.second.queue) result.queuedFds += p.fds.size();
        for (const Packet& p : entry.second.output) result.queuedFds += p.fds.size();
    }
    for (const auto& entry : processes) {
        result.handles += entry.second.handles.size();
        result.registeredLoopers += entry.second.registeredThreads;
        result.deaths += entry.second.deaths.size() + entry.second.notifiedCookies.size();
        for (const Packet& p : entry.second.queue) result.queuedFds += p.fds.size();
    }
    for (const auto& entry : buffers) {
        const Buffer& buffer = entry.second;
        if (buffer.bytes <= result.largestBufferOwnerBytes) continue;
        result.largestBufferId = buffer.id;
        result.largestBufferOwnerProcess = buffer.owner;
        auto sender = processes.find(buffer.senderProcess);
        result.largestBufferSenderPid = sender == processes.end() ? 0 : sender->second.pid;
        result.largestBufferOwnerPid = processes.at(buffer.owner).pid;
        result.largestBufferOwnerBytes = buffer.bytes;
        result.largestBufferCommand = buffer.command;
        result.largestBufferTransactionCode = buffer.transactionCode;
        result.largestBufferFlags = buffer.flags;
        result.largestBufferDataBytes = buffer.dataBytes;
        result.largestBufferOffsetsBytes = buffer.offsetsBytes;
        result.largestBufferCallId = buffer.call;
        result.largestBufferIsReply = buffer.command == BC_REPLY;
    }
    for (const auto& entry : nodes) {
        result.pendingReferences += entry.second.pendingStrong + entry.second.pendingWeak;
        for (const Packet& p : entry.second.asyncQueue) result.queuedFds += p.fds.size();
    }
    return result;
}
int Broker::run(const char* path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (!path || path[0] != '/' || strlen(path) >= sizeof(address.sun_path)) return 2;
    strcpy(address.sun_path, path);
    listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listener < 0 || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        chmod(path, 0600) != 0 || listen(listener, 32) != 0) { perror("broker listen"); return 2; }
    if (!poller.valid() || !poller.add(listener, 0)) {
        perror("broker epoll"); close(listener); unlink(path); return 2;
    }
    signal(SIGTERM, stop); signal(SIGINT, stop);
    printf("binder_broker_ready=1\nbinder_broker_pid=%d\nbinder_broker_uid=%u\n",
           getpid(), getuid()); fflush(stdout);
    std::vector<uint64_t> pending;
    while (!stopped) {
        dispatch();
        pending.clear();
        pending.swap(outputReady_);
        for (uint64_t id : pending) {
            auto connection = connections.find(id);
            if (connection == connections.end()) continue;
            connection->second.outputPending = false;
            for (int budget = 0; budget < 32 && !connection->second.output.empty(); ++budget) {
                if (sendPacket(connection->second.fd, connection->second.output.front(), MSG_DONTWAIT))
                    connection->second.output.pop_front();
                else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) closeConnection(id);
                    break;
                }
            }
            connection = connections.find(id);
            if (connection == connections.end()) continue;
            bool blocked = !connection->second.output.empty();
            if (blocked != connection->second.writeBlocked) {
                if (!poller.writable(connection->second.fd, id, blocked)) { closeConnection(id); continue; }
                connection->second.writeBlocked = blocked;
            }
        }
        if (!dispatchReady_.empty() || !outputReady_.empty()) continue;
        epoll_event events[64];
        int ready = poller.wait(events, 64);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("broker epoll wait"); break;
        }
        for (int n = 0; n < ready; ++n) {
            uint64_t id = events[n].data.u64;
            uint32_t flags = events[n].events;
            if (!id) { if (flags & EPOLLIN) acceptConnection(); continue; }
            auto connection = connections.find(id);
            if (connection == connections.end()) continue;
            if (flags & EPOLLIN) {
                Packet packet;
                if (receivePacket(connection->second.fd, &packet, MSG_DONTWAIT)) request(id, std::move(packet));
                else if (errno != EAGAIN && errno != EWOULDBLOCK) { closeConnection(id); continue; }
            }
            connection = connections.find(id);
            if (connection == connections.end()) continue;
            if ((flags & EPOLLOUT) && !connection->second.output.empty()) {
                scheduleOutput(id);
            }
            if (flags & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) closeConnection(id);
        }
    }
    while (!processes.empty()) closeProcess(processes.begin()->first);
    for (auto& entry : connections) close(entry.second.fd);
    connections.clear(); close(listener); unlink(path);
    Stats final = statistics();
    printf("binder_broker_stopped=1\nbinder_transactions=%llu\nbinder_buffers_freed=%llu\n",
           static_cast<unsigned long long>(final.transactions), static_cast<unsigned long long>(final.freed));
    return 0;
}
}
