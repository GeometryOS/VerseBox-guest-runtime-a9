#pragma once
#include <sys/types.h>
#include <linux/android/binder.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <utility>

namespace a10binder {
constexpr uint32_t kMagic = 0x41313042;
constexpr uint32_t kVersion = 3;
constexpr size_t kMaxPacket = 2 * 1024 * 1024;
constexpr size_t kInlinePacket = 64 * 1024;
constexpr uint32_t kSharedPayload = 1;
constexpr size_t kMaxFds = 128;
constexpr size_t kArenaLimit = 1024 * 1024;
constexpr uint64_t kCommandRead = 1;
enum Kind : uint32_t { Hello = 1, Command, Read, Context, MaxThreads, Exit, Inspect, Ack, Event, Wakeup, Rearm, CommandRead };
struct Header {
    uint32_t magic = kMagic, version = kVersion, kind = 0, command = 0;
    int32_t status = 0;
    uint32_t reserved = 0;
    uint64_t token = 0, arg = 0;
};
struct Stats {
    uint64_t processes, threads, nodes, handles, buffers, calls, deaths, queuedFds;
    uint64_t transactions, replies, oneways, freed, disconnected;
    uint64_t pendingReferences, registeredLoopers, spawns;
    uint64_t largestBufferOwnerPid, largestBufferOwnerBytes;
    uint64_t largestBufferCommand, largestBufferDataBytes;
    uint64_t largestBufferId, largestBufferOwnerProcess, largestBufferSenderPid;
    uint64_t largestBufferTransactionCode, largestBufferFlags;
    uint64_t largestBufferOffsetsBytes, largestBufferCallId, largestBufferIsReply;
};
struct Packet {
    Header header;
    std::vector<uint8_t> data;
    std::vector<int> fds;
    Packet() = default;
    ~Packet();
    Packet(const Packet&) = delete;
    Packet& operator=(const Packet&) = delete;
    Packet(Packet&& other) noexcept;
    Packet& operator=(Packet&& other) noexcept;
    void append(const void* bytes, size_t length) {
        const auto* begin = static_cast<const uint8_t*>(bytes);
        if (length) data.insert(data.end(), begin, begin + length);
    }
    template<class T> void append(const T& value) { append(&value, sizeof(value)); }
    template<class T> bool get(size_t offset, T* value) const {
        if (offset > data.size() || sizeof(T) > data.size() - offset) return false;
        memcpy(value, data.data() + offset, sizeof(T));
        return true;
    }
};
size_t aligned(size_t value);
inline bool isIncomingTransaction(uint32_t command) {
    return command == BR_TRANSACTION || command == BR_TRANSACTION_SEC_CTX;
}
int connectSocket(const char* path);
bool sendPacket(int fd, const Packet& packet, int flags = 0);
bool receivePacket(int fd, Packet* packet, int flags = 0);
bool transactionLayout(const Packet& packet, binder_transaction_data* transaction);
bool serializeTransaction(uint32_t command, const binder_transaction_data& transaction, Packet* packet,
                          size_t buffersSize = 0);
int inspect(const char* path, Stats* stats);
}
