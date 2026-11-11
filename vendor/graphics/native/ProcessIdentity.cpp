#include "ProcessIdentity.h"
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>
#include <sys/system_properties.h>
#include <unistd.h>

void guardParent() {
    const auto parent = getppid();
    if (parent == 1 || getppid() != parent) {
        throw std::runtime_error("Probe parent exited");
    }
}

void printIdentity(const char* nonce, bool mappings) {
    Dl_info libc{};
    if (!dladdr(reinterpret_cast<void*>(&getpid), &libc) || !libc.dli_fname) {
        throw std::runtime_error("Missing libc identity");
    }
    char sdk[PROP_VALUE_MAX]{};
    __system_property_get("ro.build.version.sdk", sdk);
    std::printf("nonce=%s\npid=%d\nuid=%d\neuid=%d\nvisible_sdk=%s\nlibc=%s\n",
                nonce, getpid(), getuid(), geteuid(), sdk, libc.dli_fname);
    const char* paths[] = {"/proc/self/status", "/proc/self/attr/current", "/proc/self/maps"};
    for (int kind = 0; kind < (mappings ? 3 : 2); ++kind) {
        FILE* file = std::fopen(paths[kind], "r");
        if (!file) throw std::runtime_error("Missing process identity file");
        char line[2048];
        bool found = false;
        while (std::fgets(line, sizeof(line), file)) {
            unsigned long long caps;
            if (kind == 0 && std::sscanf(line, "CapEff: %llx", &caps) == 1) {
                std::printf("cap_eff=%016llx\n", caps);
                found = true;
            } else if (kind == 1) {
                line[std::strcspn(line, "\r\n")] = '\0';
                std::printf("selinux=%s\n", line);
                found = true;
            } else if (kind == 2) {
                std::printf("MAP\t%s", line);
                found = true;
            }
        }
        std::fclose(file);
        if (!found) throw std::runtime_error("Incomplete process identity");
    }
}
