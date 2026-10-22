#include <errno.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
uid_t identity(const char* key, long syscallNumber) {
    const char* value = getenv("A10VM_ROOTFS") ? getenv(key) : nullptr;
    if (!value || !*value) return syscall(syscallNumber);
    int saved = errno;
    char* end = nullptr;
    unsigned long id = strtoul(value, &end, 10);
    bool valid = *end == '\0' && value[0] != '-' && id < 0x7fffffffUL;
    errno = saved;
    return valid ? static_cast<uid_t>(id) : syscall(syscallNumber);
}
}

uid_t getuid() { return identity("A10VM_GUEST_UID", __NR_getuid); }
uid_t geteuid() { return identity("A10VM_GUEST_UID", __NR_geteuid); }
gid_t getgid() { return identity("A10VM_GUEST_GID", __NR_getgid); }
gid_t getegid() { return identity("A10VM_GUEST_GID", __NR_getegid); }
