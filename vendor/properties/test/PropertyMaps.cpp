#include "ProbeChecks.h"
#include <cstring>
#include <sys/stat.h>
#include <sys/sysmacros.h>

int mappingCount(const std::string& directory, const char* permissions) {
    struct stat files[3]{};
    const char* names[] = {"property_info", "properties_serial", "a10vm_private_prop"};
    for (int i = 0; i < 3; ++i) {
        if (stat((directory + "/" + names[i]).c_str(), &files[i]) != 0) {
            throw std::runtime_error("property mapping stat");
        }
    }
    FILE* file = std::fopen("/proc/self/maps", "r");
    if (!file) throw std::runtime_error("property maps");
    char line[2048];
    int count = 0;
    while (std::fgets(line, sizeof(line), file)) {
        char access[5];
        unsigned deviceMajor, deviceMinor;
        unsigned long inode;
        if (std::sscanf(line, "%*s %4s %*s %x:%x %lu", access,
                        &deviceMajor, &deviceMinor, &inode) != 4) continue;
        for (const auto& entry : files) {
            if (entry.st_ino == inode && major(entry.st_dev) == deviceMajor &&
                    minor(entry.st_dev) == deviceMinor &&
                    (!permissions || std::strcmp(permissions, access) == 0)) {
                if (permissions) std::printf("PROPERTY_MAP\t%s", line);
                ++count;
            }
        }
    }
    std::fclose(file);
    return count;
}
