#pragma once
#include "Wire.h"
#include "Driver.h"
#include <map>
#include <mutex>

namespace a10binder {
class ClientBuffers {
public:
    bool receive(Packet* packet, binder_transaction_data* transaction, binder_uintptr_t* securityContext = nullptr);
    bool token(uintptr_t address, uint64_t* token);
    void release(uintptr_t address);
    void stats(a10binder_buffer_stats* out);
private:
    struct Buffer { uint64_t token; size_t size; std::vector<int> arrayFds; };
    std::mutex mutex;
    std::map<uintptr_t, Buffer> buffers;
    a10binder_buffer_stats counts{};
};
ClientBuffers& clientBuffers();
}
