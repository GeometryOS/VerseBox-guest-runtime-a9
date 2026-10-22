#include "PrivateProperties.h"
#include "PropertyInfo.h"
#include <cerrno>
#include <cstring>
#include <sys/stat.h>

bool PrivateProperties::create(const std::string& directory) {
    if (opened_) return false;
    if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) return false;
    if (!writePropertyInfo(directory)) return false;
    bool relabelFailed = false;
    writable_ = properties_.AreaInit(directory.c_str(), &relabelFailed) && !relabelFailed;
    opened_ = writable_;
    return writable_;
}

bool PrivateProperties::open(const std::string& directory) {
    if (opened_) return false;
    opened_ = properties_.Init(directory.c_str());
    return opened_;
}

bool PrivateProperties::set(const std::string& name, const std::string& value) {
    if (!writable_ || name.empty()) return false;
    const auto* property = properties_.Find(name.c_str());
    if (property) {
        if (name.compare(0, 3, "ro.") == 0) return false;
        return properties_.Update(const_cast<prop_info*>(property), value.c_str(), value.size()) == 0;
    }
    return properties_.Add(name.c_str(), name.size(), value.c_str(), value.size()) == 0;
}

std::string PrivateProperties::get(const char* name) {
    std::string value;
    const auto* property = find(name);
    if (property) {
        properties_.ReadCallback(property, [](void* cookie, const char*, const char* text, uint32_t) {
            *static_cast<std::string*>(cookie) = text;
        }, &value);
    }
    return value;
}

int PrivateProperties::count() {
    int count = 0;
    int result = properties_.Foreach([](const prop_info*, void* cookie) {
        ++*static_cast<int*>(cookie);
    }, &count);
    return result == 0 ? count : -1;
}
