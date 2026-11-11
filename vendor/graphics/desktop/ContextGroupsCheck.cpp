#include "ContextGroupsCheck.h"
#include "GpuBackend.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

void checkContextGroups(GpuBackend& backend) {
    std::vector<uint32_t> contexts;
    const auto baseline = backend.resourceCounts().contexts;
    try {
        for (int group = 0; group < 24; ++group) {
            const auto parent = backend.context(0, 0, 2);
            if (!parent) throw std::runtime_error("Independent application context allocation");
            contexts.push_back(parent);
            const auto worker = backend.context(0, parent, 2);
            if (!worker) throw std::runtime_error("Shared application worker context allocation");
            contexts.push_back(worker);
        }
        if (backend.resourceCounts().contexts != baseline + contexts.size())
            throw std::runtime_error("Application context registration");
    } catch (...) {
        for (auto context : contexts) backend.destroyContext(context);
        throw;
    }
    for (auto context : contexts) backend.destroyContext(context);
    if (backend.resourceCounts().contexts != baseline)
        throw std::runtime_error("Application context cleanup");
    std::puts("desktop_context_groups_capacity=48\ndesktop_context_groups_cleanup=1");
}
