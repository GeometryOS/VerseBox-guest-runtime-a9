#pragma once
#include "Wire.h"

namespace a10binder {
struct TransactionObject {
    size_t offset;
    uint32_t type;
    binder_buffer_object buffer{};
    binder_fd_array_object fdArray{};
};
struct TransactionLayout {
    binder_transaction_data transaction{};
    size_t buffersOffset = 0;
    size_t securityContextOffset = 0;
    std::vector<TransactionObject> objects;
};
bool transactionObjects(const Packet& packet, TransactionLayout* layout);
bool validateTransaction(const Packet& packet, TransactionLayout* layout);
}
