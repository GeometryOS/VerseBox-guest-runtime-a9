#include "TransactionLayout.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>

namespace a10binder {
namespace {
bool copyDescriptor(Packet* packet, size_t offset) {
    int source;
    if (!packet->get(offset, &source)) { errno = EINVAL; return false; }
    if (packet->fds.size() == kMaxFds) { errno = EMSGSIZE; return false; }
    int fd = fcntl(source, F_DUPFD_CLOEXEC, 0);
    if (fd < 0) return false;
    int index = packet->fds.size();
    packet->fds.push_back(fd);
    memcpy(packet->data.data() + offset, &index, sizeof(index));
    return true;
}
}

bool serializeTransaction(uint32_t command, const binder_transaction_data& source, Packet* packet,
                          size_t buffersSize) {
    if (source.data_size > kMaxPacket / 2 || source.offsets_size > kMaxPacket / 2 ||
        source.offsets_size % sizeof(binder_size_t) || buffersSize > kMaxPacket || buffersSize % 8 ||
        sizeof(Header) + sizeof(source) + aligned(source.data_size) + source.offsets_size + buffersSize > kMaxPacket) {
        errno = EMSGSIZE;
        return false;
    }
    if ((source.data_size && !source.data.ptr.buffer) || (source.offsets_size && !source.data.ptr.offsets)) {
        errno = EFAULT;
        return false;
    }
    Packet encoded;
    encoded.header = packet->header;
    encoded.header.command = command;
    binder_transaction_data wire = source;
    wire.data.ptr.buffer = wire.data.ptr.offsets = 0;
    wire.sender_pid = wire.sender_euid = 0;
    encoded.append(wire);
    encoded.append(reinterpret_cast<const void*>(source.data.ptr.buffer), source.data_size);
    encoded.data.resize(sizeof(wire) + aligned(source.data_size), 0);
    encoded.append(reinterpret_cast<const void*>(source.data.ptr.offsets), source.offsets_size);
    TransactionLayout layout;
    if (!transactionObjects(encoded, &layout)) return false;
    size_t used = 0;
    for (const auto& object : layout.objects) {
        if (object.type != BINDER_TYPE_PTR) continue;
        if (!object.buffer.buffer) { errno = EFAULT; return false; }
        size_t size = aligned(object.buffer.length);
        if (size > buffersSize - used) { errno = EINVAL; return false; }
        used += size;
    }
    if (used != buffersSize) { errno = EINVAL; return false; }
    encoded.data.reserve(encoded.data.size() + buffersSize);
    for (auto& object : layout.objects) {
        if (object.type == BINDER_TYPE_PTR) {
            auto buffer = object.buffer;
            buffer.buffer = encoded.data.size();
            encoded.append(reinterpret_cast<const void*>(object.buffer.buffer), buffer.length);
            encoded.data.resize(buffer.buffer + aligned(buffer.length), 0);
            object.buffer = buffer;
            memcpy(encoded.data.data() + object.offset, &buffer, sizeof(buffer));
            if (buffer.flags & BINDER_BUFFER_FLAG_HAS_PARENT) {
                const auto& parent = layout.objects[buffer.parent].buffer;
                memcpy(encoded.data.data() + parent.buffer + buffer.parent_offset, &buffer.buffer, sizeof(buffer.buffer));
            }
        } else if (object.type == BINDER_TYPE_FDA) {
            const auto& array = object.fdArray;
            const auto& parent = layout.objects[array.parent].buffer;
            for (size_t n = 0; n < array.num_fds; ++n) {
                if (!copyDescriptor(&encoded, parent.buffer + array.parent_offset + n * sizeof(int))) return false;
            }
        } else if (object.type == BINDER_TYPE_FD) {
            if (!copyDescriptor(&encoded, object.offset + offsetof(binder_fd_object, fd))) return false;
        }
    }
    if (!validateTransaction(encoded, &layout)) return false;
    *packet = std::move(encoded);
    return true;
}
}
