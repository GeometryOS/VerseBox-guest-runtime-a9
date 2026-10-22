#pragma once
#include <stdint.h>
#include <string>

class HostDeviceSocket {
    int fd = -1;
    bool transfer(void* data, size_t size, bool writing);
public:
    HostDeviceSocket(const char* name, uint32_t magic, uint32_t command, bool polling = false);
    ~HostDeviceSocket();
    HostDeviceSocket(const HostDeviceSocket&) = delete;
    HostDeviceSocket& operator=(const HostDeviceSocket&) = delete;
    bool valid() const { return fd >= 0; }
    bool put(uint32_t value);
    bool put64(uint64_t value);
    bool get(int32_t* value);
    bool get64(int64_t* value);
    bool getFloat(float* value);
    int getDescriptor();
    bool getString(std::string* value, int32_t limit = 4096);
    bool getBytes(void* data, size_t size) { return transfer(data, size, false); }
    bool putString(const std::string& value) {
        return put(value.size()) && transfer(const_cast<char*>(value.data()), value.size(), true);
    }
    void cancel();
};
