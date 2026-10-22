#!/bin/sh
# Shared by the bootstrap, linker, and dynamic libc builders.
guest_fs_replaces() {
    case "$1" in
        bionic/libc/arch-arm64/syscalls/__openat.S|bionic/libc/arch-arm64/syscalls/___faccessat.S|\
        bionic/libc/arch-arm64/syscalls/fstatat64.S|bionic/libc/arch-arm64/syscalls/readlinkat.S|\
        bionic/libc/arch-arm64/syscalls/chdir.S|bionic/libc/arch-arm64/syscalls/__getcwd.S|\
        bionic/libc/arch-arm64/syscalls/mkdirat.S|bionic/libc/arch-arm64/syscalls/unlinkat.S|\
        bionic/libc/arch-arm64/syscalls/symlinkat.S|\
        bionic/libc/arch-arm64/syscalls/renameat.S|bionic/libc/arch-arm64/syscalls/execve.S|\
        bionic/libc/arch-arm64/syscalls/___fchmodat.S|bionic/libc/arch-arm64/syscalls/fchownat.S|\
        bionic/libc/arch-arm64/syscalls/fchown.S|bionic/libc/arch-arm64/syscalls/fstat64.S|\
        bionic/libc/arch-arm64/syscalls/utimensat.S|bionic/libc/arch-arm64/syscalls/__statfs.S|\
        bionic/libc/arch-arm64/syscalls/truncate.S|\
        bionic/libc/arch-arm64/syscalls/setxattr.S|bionic/libc/arch-arm64/syscalls/lsetxattr.S|\
        bionic/libc/arch-arm64/syscalls/getxattr.S|bionic/libc/arch-arm64/syscalls/lgetxattr.S|\
        bionic/libc/arch-arm64/syscalls/listxattr.S|bionic/libc/arch-arm64/syscalls/llistxattr.S|\
        bionic/libc/arch-arm64/syscalls/removexattr.S|bionic/libc/arch-arm64/syscalls/lremovexattr.S|\
        bionic/libc/arch-arm64/syscalls/timerfd_create.S|bionic/libc/arch-arm64/syscalls/settimeofday.S|\
        bionic/libc/arch-arm64/syscalls/inotify_add_watch.S|\
        bionic/libc/arch-arm64/syscalls/bind.S|bionic/libc/arch-arm64/syscalls/__connect.S|\
        bionic/libc/arch-arm64/syscalls/getuid.S|bionic/libc/arch-arm64/syscalls/geteuid.S|\
        bionic/libc/arch-arm64/syscalls/getgid.S|bionic/libc/arch-arm64/syscalls/getegid.S) return 0 ;;
        *) return 1 ;;
    esac
}
