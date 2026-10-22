#include "GuestOwnership.h"
#include "GuestPaths.h"
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" const char a10vm_owner_attribute[];
extern "C" const char a10vm_owner_xattr_return[];

namespace {
long installFilter(long number, long operation, long flags, const sock_fprog* original,
                   long d, long e, long f) {
    uintptr_t address = reinterpret_cast<uintptr_t>(a10vm_owner_xattr_return);
    uintptr_t name = reinterpret_cast<uintptr_t>(a10vm_owner_attribute);
    // stat needs this fixed private attribute even when the caller denies xattrs.
    // Every other call enters the original program with its initial accumulator.
    sock_filter prefix[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 0, 0),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_getxattr, 1, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_fgetxattr, 0, 0),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, instruction_pointer)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t>(address), 0, 0),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, instruction_pointer) + 4),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t>(address >> 32), 0, 0),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[1])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t>(name), 0, 0),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[1]) + 4),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t>(name >> 32), 0, 0),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[3])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, guestOwnershipSize, 0, 0),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args[3]) + 4),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_LD | BPF_IMM, 0),
    };
    constexpr size_t count = sizeof(prefix) / sizeof(prefix[0]);
    if (!original || !original->filter || !original->len || original->len > BPF_MAXINSNS - count)
        return syscall(number, operation, flags, original, d, e, f);
    for (size_t i = 0; i < count; ++i) {
        if (prefix[i].code == (BPF_JMP | BPF_JEQ | BPF_K) && i != 3)
            prefix[i].jf = static_cast<uint8_t>(count - i - 2);
    }
    auto* instructions = static_cast<sock_filter*>(malloc((count + original->len) * sizeof(sock_filter)));
    if (!instructions) { errno = ENOMEM; return -1; }
    memcpy(instructions, prefix, sizeof(prefix));
    memcpy(instructions + count, original->filter, original->len * sizeof(sock_filter));
    sock_fprog adapted{static_cast<unsigned short>(count + original->len), instructions};
    long result = syscall(number, operation, flags, &adapted, d, e, f);
    int saved = errno;
    free(instructions);
    errno = saved;
    return result;
}
}

extern "C" long a10vm_sandbox_syscall(long number, long operation, long flags, long filter,
                                      long d, long e, long f) {
    bool installing = (number == __NR_seccomp && operation == SECCOMP_SET_MODE_FILTER) ||
        (number == __NR_prctl && operation == PR_SET_SECCOMP && flags == SECCOMP_MODE_FILTER);
    if (guestRoot() && installing)
        return installFilter(number, operation, flags, reinterpret_cast<const sock_fprog*>(filter), d, e, f);
    return syscall(number, operation, flags, filter, d, e, f);
}

extern "C" int a10vm_sandbox_prctl(long operation, long flags, long filter, long d, long e) {
    return a10vm_sandbox_syscall(__NR_prctl, operation, flags, filter, d, e, 0);
}
