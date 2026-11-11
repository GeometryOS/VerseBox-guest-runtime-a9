#include "GuestTextureContexts.h"
#include <map>
#include <mutex>

namespace guesttextures {
namespace {
std::mutex mutex;
std::map<const GLSharedGroup*, unsigned> contexts;
}
void added(const GLSharedGroup* group) {
    std::lock_guard<std::mutex> lock(mutex);
    ++contexts[group];
}
void removed(const GLSharedGroup* group) {
    std::lock_guard<std::mutex> lock(mutex);
    auto found = contexts.find(group);
    if (found != contexts.end() && --found->second == 0) contexts.erase(found);
}
bool shared(const GLSharedGroup* group) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto found = contexts.find(group);
    return found != contexts.end() && found->second > 1;
}
}
