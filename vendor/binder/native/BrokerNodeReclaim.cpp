#include "Broker.h"

namespace a10binder {
void Broker::scheduleNodeReclaim(Node& node) {
    if (node.reclaimPending) return;
    reclaimReady_.push_back(node.id);
    node.reclaimPending = true;
}

void Broker::releaseNodeReference(uint64_t id) {
    Node& node = nodes.at(id);
    --node.containerReferences;
    scheduleNodeReclaim(node);
}

void Broker::collectDeadNodes() {
    while (!reclaimReady_.empty()) {
        uint64_t id = reclaimReady_.back();
        reclaimReady_.pop_back();
        auto entry = nodes.find(id);
        if (entry == nodes.end()) continue;
        Node& node = entry->second;
        node.reclaimPending = false;
        if (id == contextNode || node.strong || node.weak || node.pendingStrong ||
            node.pendingWeak || node.asyncBusy || !node.asyncQueue.empty() || node.containerReferences)
            continue;
        nodes.erase(entry);
    }
}
}
