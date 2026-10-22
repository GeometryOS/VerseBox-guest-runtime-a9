#include "TransactionLayout.h"
#include <errno.h>

namespace a10binder {
namespace {
bool invalid(int error = EINVAL) { errno = error; return false; }
size_t objectSize(uint32_t type) {
    switch (type) {
        case BINDER_TYPE_BINDER: case BINDER_TYPE_WEAK_BINDER:
        case BINDER_TYPE_HANDLE: case BINDER_TYPE_WEAK_HANDLE:
        case BINDER_TYPE_FD: return sizeof(flat_binder_object);
        case BINDER_TYPE_PTR: return sizeof(binder_buffer_object);
        case BINDER_TYPE_FDA: return sizeof(binder_fd_array_object);
        default: return 0;
    }
}
}

bool transactionObjects(const Packet& packet, TransactionLayout* layout) {
    auto& transaction = layout->transaction;
    if (!packet.get(0, &transaction) || transaction.data_size > kMaxPacket / 2 ||
        transaction.offsets_size > kMaxPacket / 2 || transaction.offsets_size % sizeof(binder_size_t) ||
        packet.data.size() + sizeof(Header) > kMaxPacket) return invalid();
    size_t offsets = sizeof(transaction) + aligned(transaction.data_size);
    layout->buffersOffset = offsets + transaction.offsets_size;
    if (layout->buffersOffset > packet.data.size()) return invalid();
    layout->objects.clear();
    size_t previousEnd = 0;
    std::vector<size_t> nextFixup;
    for (size_t at = 0; at < transaction.offsets_size; at += sizeof(binder_size_t)) {
        binder_size_t offset;
        binder_object_header header;
        if (!packet.get(offsets + at, &offset) || offset % 4 || offset < previousEnd ||
            offset > transaction.data_size || sizeof(header) > transaction.data_size - offset ||
            !packet.get(sizeof(transaction) + offset, &header)) return invalid();
        size_t size = objectSize(header.type);
        if (!size) return invalid(ENOTSUP);
        if (size > transaction.data_size - offset) return invalid();
        previousEnd = offset + size;
        TransactionObject object{sizeof(transaction) + offset, header.type};
        bool hasParent = false;
        size_t parent = 0, parentOffset = 0, width = 0, alignment = 0;
        if (header.type == BINDER_TYPE_PTR) {
            packet.get(object.offset, &object.buffer);
            if (object.buffer.flags & ~BINDER_BUFFER_FLAG_HAS_PARENT ||
                object.buffer.length > kMaxPacket) return invalid();
            hasParent = object.buffer.flags & BINDER_BUFFER_FLAG_HAS_PARENT;
            parent = object.buffer.parent; parentOffset = object.buffer.parent_offset;
            width = alignment = sizeof(binder_uintptr_t);
        } else if (header.type == BINDER_TYPE_FDA) {
            packet.get(object.offset, &object.fdArray);
            if (object.fdArray.num_fds > kMaxFds) return invalid(EMSGSIZE);
            hasParent = true;
            parent = object.fdArray.parent; parentOffset = object.fdArray.parent_offset;
            width = object.fdArray.num_fds * sizeof(int); alignment = sizeof(int);
        }
        if (hasParent) {
            if (parent >= layout->objects.size() || layout->objects[parent].type != BINDER_TYPE_PTR)
                return invalid();
            const auto& buffer = layout->objects[parent].buffer;
            if (parentOffset % alignment || parentOffset < nextFixup[parent] ||
                parentOffset > buffer.length || width > buffer.length - parentOffset) return invalid();
            nextFixup[parent] = parentOffset + width;
        }
        layout->objects.push_back(object);
        nextFixup.push_back(0);
    }
    return true;
}

bool validateTransaction(const Packet& packet, TransactionLayout* layout) {
    if (!transactionObjects(packet, layout)) return false;
    layout->securityContextOffset = 0;
    size_t cursor = layout->buffersOffset, fdCount = 0;
    for (const auto& object : layout->objects) {
        if (object.type == BINDER_TYPE_PTR) {
            const auto& buffer = object.buffer;
            size_t size = aligned(buffer.length);
            if (buffer.buffer != cursor || size > packet.data.size() - cursor) return invalid();
            cursor += size;
            if (buffer.flags & BINDER_BUFFER_FLAG_HAS_PARENT) {
                const auto& parent = layout->objects[buffer.parent].buffer;
                binder_uintptr_t value;
                if (!packet.get(parent.buffer + buffer.parent_offset, &value) || value != buffer.buffer)
                    return invalid();
            }
        } else if (object.type == BINDER_TYPE_FDA) {
            const auto& array = object.fdArray;
            const auto& parent = layout->objects[array.parent].buffer;
            for (size_t n = 0; n < array.num_fds; ++n) {
                uint32_t value;
                if (!packet.get(parent.buffer + array.parent_offset + n * sizeof(int), &value) ||
                    value != fdCount++) return invalid();
            }
        } else if (object.type == BINDER_TYPE_FD) {
            binder_fd_object fd;
            packet.get(object.offset, &fd);
            if (fd.fd != fdCount++) return invalid();
        }
    }
    if (packet.header.command == BR_TRANSACTION_SEC_CTX) {
        size_t bytes = packet.data.size() - cursor;
        if (bytes <= 1 || bytes > 4096 || packet.data.back() != 0 ||
            memchr(packet.data.data() + cursor, 0, bytes - 1)) return invalid();
        layout->securityContextOffset = cursor;
        cursor = packet.data.size();
    }
    if (cursor != packet.data.size() || fdCount != packet.fds.size()) return invalid();
    return true;
}

bool transactionLayout(const Packet& packet, binder_transaction_data* transaction) {
    TransactionLayout layout;
    if (!validateTransaction(packet, &layout)) return false;
    *transaction = layout.transaction;
    return true;
}
}
