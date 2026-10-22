#pragma once
#include <string>
#include <system_properties/system_properties.h>

// One writer owns each instance; readers only map the instance files read-only.
class PrivateProperties {
public:
    ~PrivateProperties() { properties_.Reset(); }
    bool create(const std::string& directory);
    bool open(const std::string& directory);
    bool set(const std::string& name, const std::string& value);
    std::string get(const char* name);
    const prop_info* find(const char* name) { return properties_.Find(name); }
    uint32_t serial(const prop_info* property) { return properties_.Serial(property); }
    uint32_t areaSerial() { return properties_.AreaSerial(); }
    bool wait(const prop_info* property, uint32_t oldSerial, uint32_t* newSerial,
              const timespec* timeout) {
        return properties_.Wait(property, oldSerial, newSerial, timeout);
    }
    int count();

private:
    SystemProperties properties_{false};
    bool writable_ = false;
    bool opened_ = false;
};
