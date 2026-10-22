#pragma once
#include "Wire.h"
#include "BrokerPoller.h"
#include <sys/socket.h>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace a10binder {
struct Reference { uint64_t node; uint32_t handle; bool strong; };
struct Handle { uint64_t node; uint32_t strong = 0, weak = 0, temporary = 0; };
struct Death { uint32_t handle; uint64_t node, cookie; bool notified = false; };
struct Process {
    uint64_t id, control;
    pid_t pid;
    uid_t uid;
    uid_t guestUid = uid;
    uint32_t maxThreads = 0, nextHandle = 1;
    uint32_t registeredThreads = 0;
    bool spawnPending = false, notified = false;
    size_t bufferBytes = 0;
    std::map<uint32_t, Handle> handles;
    std::vector<Death> deaths;
    std::set<uint64_t> notifiedCookies;
    std::deque<Packet> queue;
    std::set<uint64_t> threads;
    bool dispatchPending = false;
};
struct Connection {
    int fd;
    ucred peer;
    uint64_t process = 0, tid = 0;
    bool control = false, looper = false, registered = false;
    bool writeBlocked = false;
    bool outputPending = false;
    size_t readCapacity = 0;
    std::deque<Packet> queue, output;
    std::vector<uint64_t> incoming;
    std::vector<uint64_t> outgoing;
};
struct Node {
    uint64_t id, owner, exporter, ptr, cookie;
    uint32_t flags;
    uint64_t strong = 0, weak = 0;
    uint32_t pendingStrong = 0, pendingWeak = 0;
    bool alive = true, asyncBusy = false;
    bool heldStrong = false, heldWeak = false;
    std::deque<Packet> asyncQueue;
    uint64_t containerReferences = 0;
    bool reclaimPending = false;
};
struct Buffer {
    uint64_t owner, asyncNode;
    size_t bytes;
    uint64_t id = 0, senderProcess = 0, call = 0;
    uint32_t command = 0, transactionCode = 0, flags = 0;
    size_t dataBytes = 0, offsetsBytes = 0;
    std::vector<Reference> references;
};
struct Call { uint64_t from, receiver, targetProcess, parent; uint32_t flags; };
class Broker {
public:
    int run(const char* path);
private:
    friend class BrokerDispatchTest;
    friend class BrokerCleanupTest;
    friend class BrokerReclaimBenchmark;
    friend class BinderSecurityContextTest;
    uint64_t nextId = 1, contextNode = 0;
    int listener = -1;
    BrokerPoller poller;
    Stats counters{};
    std::map<uint64_t, Connection> connections;
    std::map<uint64_t, Process> processes;
    std::map<uint64_t, Node> nodes;
    std::map<uint64_t, Buffer> buffers;
    std::map<uint64_t, Call> calls;
    std::vector<uint64_t> dispatchReady_, outputReady_, reclaimReady_;
    void acceptConnection();
    void request(uint64_t connection, Packet packet);
    void acknowledge(uint64_t connection, int status = 0, uint64_t token = 0, uint64_t capabilities = 0);
    void event(uint64_t connection, uint32_t code, const void* data = nullptr, size_t size = 0);
    void processEvent(uint64_t process, Packet packet);
    void threadEvent(uint64_t connection, Packet packet);
    void queueOutput(uint64_t connection, Packet packet);
    void scheduleOutput(uint64_t connection);
    void scheduleDispatch(uint64_t process);
    void dispatch();
    int command(uint64_t connection, Packet packet);
    int transact(uint64_t connection, Packet packet);
    int translate(uint64_t source, uint64_t destination, Packet* packet, Buffer* buffer);
    uint64_t resolve(uint64_t process, uint32_t handle);
    uint64_t exportNode(uint64_t connection, const flat_binder_object& object);
    uint32_t importHandle(uint64_t process, uint64_t node);
    void adjust(uint64_t node, bool strong, int change);
    void referenceEvent(Node& node, uint32_t command);
    void updateReferences(Node& node);
    void trimHandle(uint64_t process, uint32_t handle);
    int freeBuffer(uint64_t process, uint64_t buffer);
    void closeConnection(uint64_t connection);
    void closeProcess(uint64_t process);
    void finishCall(uint64_t call);
    void scheduleNodeReclaim(Node& node);
    void releaseNodeReference(uint64_t node);
    void collectDeadNodes();
    Stats statistics();
};
}
