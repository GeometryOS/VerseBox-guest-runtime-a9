#include <private/bionic_asm.h>
#include <asm/unistd.h>

ENTRY(syscall)
    cmp x0, #__NR_seccomp
    b.eq a10vm_sandbox_syscall
    cmp x0, #__NR_prctl
    b.eq a10vm_sandbox_syscall
    cmp x0, #__NR_openat
    b.eq a10vm_path_syscall
    cmp x0, #__NR_newfstatat
    b.eq a10vm_path_syscall
    cmp x0, #__NR_readlinkat
    b.eq a10vm_path_syscall
    cmp x0, #__NR_symlinkat
    b.eq a10vm_path_syscall
    cmp x0, #__NR_mkdirat
    b.eq a10vm_path_syscall
    cmp x0, #__NR_unlinkat
    b.eq a10vm_path_syscall
    cmp x0, #__NR_renameat
    b.eq a10vm_path_syscall
    cmp x0, #__NR_renameat2
    b.eq a10vm_path_syscall
    cmp x0, #__NR_faccessat
    b.eq a10vm_path_syscall
    cmp x0, #__NR_statfs
    b.eq a10vm_path_syscall
    cmp x0, #__NR_truncate
    b.eq a10vm_path_syscall
    cmp x0, #__NR_chdir
    b.eq a10vm_path_syscall
    cmp x0, #__NR_inotify_add_watch
    b.eq a10vm_path_syscall
    cmp x0, #__NR_utimensat
    b.eq a10vm_path_syscall
    // In particular, clone must return on its new stack without a C++ frame.
    b a10vm_kernel_syscall
END(syscall)

ENTRY(prctl)
    b a10vm_sandbox_prctl
END(prctl)

ENTRY(a10vm_owner_xattr)
    mov x8, x0
    mov x0, x1
    adrp x1, a10vm_owner_attribute
    add x1, x1, :lo12:a10vm_owner_attribute
    svc #0
.global a10vm_owner_xattr_return
.hidden a10vm_owner_xattr_return
a10vm_owner_xattr_return:
    cmn x0, #(MAX_ERRNO + 1)
    cneg x0, x0, hi
    b.hi __set_errno_internal
    ret
END(a10vm_owner_xattr)

.section .rodata
.global a10vm_owner_attribute
.hidden a10vm_owner_attribute
a10vm_owner_attribute:
    .asciz "user.a10vm.owner"

.hidden a10vm_kernel_syscall
.hidden a10vm_path_syscall
.hidden a10vm_sandbox_syscall
.hidden a10vm_sandbox_prctl
.hidden a10vm_owner_xattr
