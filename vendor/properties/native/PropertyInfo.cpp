#include "PropertyInfo.h"
#include <property_info_parser/property_info_parser.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

using namespace android::properties;

bool writePropertyInfo(const std::string& directory) {
    // A single default context/type is sufficient for this runtime slice.
    std::vector<char> data(sizeof(PropertyInfoAreaHeader));
    auto append = [&](const void* value, size_t size) {
        uint32_t offset = data.size();
        const auto* bytes = static_cast<const char*>(value);
        data.insert(data.end(), bytes, bytes + size);
        while (data.size() % sizeof(uint32_t)) data.push_back(0);
        return offset;
    };
    auto stringTable = [&](const char* value) {
        uint32_t offset = append(value, std::strlen(value) + 1);
        uint32_t table[] = {1, offset};
        return append(table, sizeof(table));
    };
    auto contexts = stringTable("a10vm_private_prop");
    auto types = stringTable("string");
    auto name = append("", 1);
    PropertyEntry entry{name, 0, 0, 0};
    auto entryOffset = append(&entry, sizeof(entry));
    TrieNodeInternal root{entryOffset, 0, 0, 0, 0, 0, 0};
    auto rootOffset = append(&root, sizeof(root));
    PropertyInfoAreaHeader header{1, 1, static_cast<uint32_t>(data.size()),
                                  contexts, types, rootOffset};
    std::memcpy(data.data(), &header, sizeof(header));
    int fd = open((directory + "/property_info").c_str(),
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0444);
    if (fd < 0) return false;
    size_t offset = 0;
    while (offset < data.size()) {
        auto written = write(fd, data.data() + offset, data.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { close(fd); return false; }
        offset += written;
    }
    return close(fd) == 0;
}
