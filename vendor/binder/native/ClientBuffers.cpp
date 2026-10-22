#include "ClientBuffers.h"
#include "TransactionLayout.h"
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>

namespace a10binder {
ClientBuffers& clientBuffers() {
    // ProcessState/Parcel exit handlers still release buffers after DSO static destructors.
    static ClientBuffers* value = new ClientBuffers;
    return *value;
}
bool ClientBuffers::receive(Packet* packet, binder_transaction_data* transaction, binder_uintptr_t* securityContext) {
    if (securityContext) *securityContext = 0;
    TransactionLayout layout;
    if (!validateTransaction(*packet, &layout)) return false;
    if (layout.securityContextOffset && !securityContext) { errno = EINVAL; return false; }
    *transaction = layout.transaction;
    size_t page = sysconf(_SC_PAGESIZE);
    size_t size = (std::max<size_t>(1, packet->data.size() - sizeof(*transaction)) + page - 1) & ~(page - 1);
    std::lock_guard<std::mutex> lock(mutex);
    if (size > kArenaLimit - counts.bytes) { errno = ENOSPC; return false; }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED) return false;
    memcpy(data, packet->data.data() + sizeof(*transaction), packet->data.size() - sizeof(*transaction));
    auto* offsets = reinterpret_cast<binder_size_t*>(static_cast<uint8_t*>(data) + aligned(transaction->data_size));
    auto local = [&](size_t offset) { return static_cast<uint8_t*>(data) + offset - sizeof(*transaction); };
    std::vector<int> arrayFds;
    for (const auto& object : layout.objects) {
        if (object.type == BINDER_TYPE_PTR) {
            auto buffer = object.buffer;
            buffer.buffer = reinterpret_cast<uintptr_t>(local(buffer.buffer));
            memcpy(local(object.offset), &buffer, sizeof(buffer));
            if (buffer.flags & BINDER_BUFFER_FLAG_HAS_PARENT) {
                const auto& parent = layout.objects[buffer.parent].buffer;
                memcpy(local(parent.buffer + buffer.parent_offset), &buffer.buffer, sizeof(buffer.buffer));
            }
        } else if (object.type == BINDER_TYPE_FDA) {
            const auto& array = object.fdArray;
            const auto& parent = layout.objects[array.parent].buffer;
            for (size_t n = 0; n < array.num_fds; ++n) {
                uint8_t* location = local(parent.buffer + array.parent_offset + n * sizeof(int));
                int index; memcpy(&index, location, sizeof(index));
                int fd = packet->fds[index];
                memcpy(location, &fd, sizeof(fd));
                arrayFds.push_back(fd);
            }
        } else if (object.type == BINDER_TYPE_FD) {
            binder_fd_object fd;
            packet->get(object.offset, &fd);
            fd.fd = packet->fds[fd.fd];
            fd.cookie = 1;
            memcpy(local(object.offset), &fd, sizeof(fd));
        }
    }
    if (mprotect(data, size, PROT_READ) != 0) { munmap(data, size); return false; }
    transaction->data.ptr.buffer = reinterpret_cast<uintptr_t>(data);
    transaction->data.ptr.offsets = reinterpret_cast<uintptr_t>(offsets);
    if (layout.securityContextOffset)
        *securityContext = reinterpret_cast<uintptr_t>(local(layout.securityContextOffset));
    buffers.emplace(reinterpret_cast<uintptr_t>(data), Buffer{packet->header.token, size, std::move(arrayFds)});
    ++counts.allocated;
    ++counts.active;
    counts.bytes += size;
    // Flat FDs belong to Parcel; FD arrays belong to the transaction buffer.
    packet->fds.clear();
    return true;
}
bool ClientBuffers::token(uintptr_t address, uint64_t* token) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = buffers.find(address);
    if (it == buffers.end()) { errno = EINVAL; return false; }
    *token = it->second.token;
    return true;
}
void ClientBuffers::release(uintptr_t address) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = buffers.find(address);
    if (it == buffers.end()) return;
    for (int fd : it->second.arrayFds) close(fd);
    munmap(reinterpret_cast<void*>(address), it->second.size);
    counts.bytes -= it->second.size;
    ++counts.freed;
    --counts.active;
    buffers.erase(it);
}
void ClientBuffers::stats(a10binder_buffer_stats* out) {
    std::lock_guard<std::mutex> lock(mutex);
    *out = counts;
}
}
extern "C" void a10binder_get_buffer_stats(a10binder_buffer_stats* stats) {
    a10binder::clientBuffers().stats(stats);
}
