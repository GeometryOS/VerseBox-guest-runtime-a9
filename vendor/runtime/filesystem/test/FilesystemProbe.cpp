#include "../../test/ProbeOutput.h"
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/system_properties.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <errno.h>

int ownershipSandboxCheck(bool tsync);
int ownershipProfileSetup();

static bool contents(const char* path, const char* expected, int dir = AT_FDCWD) {
    int fd = openat(dir, path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char text[128] = {};
    ssize_t size = read(fd, text, sizeof(text) - 1);
    close(fd);
    return size >= 0 && !strcmp(text, expected);
}

static bool identity(const char* role, const char* nonce) {
    bool passed = emit("filesystem_identity_begin=%s\n", role);
    passed &= printIdentity(nonce);
    passed &= emit("filesystem_identity_end=%s\n", role);
    return passed;
}

static bool child(const char* argv0, const char* nonce) {
    char sdk[PROP_VALUE_MAX] = {};
    __system_property_get("ro.build.version.sdk", sdk);
    bool passed = check(!strcmp(argv0, "filesystem-child"), "fs_child_argv0");
    passed &= check(contents("/system/etc/a10vm-marker", nonce), "fs_child_paths");
    passed &= check(!strcmp(sdk, "29"), "fs_child_properties");
    char cwd[128];
    passed &= check(getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, "/data/local/tmp"),
                    "fs_child_cwd");
    passed &= identity("child", nonce);
    emit("filesystem_child_complete=%d\n", passed);
    return passed;
}

int main(int argc, char** argv) {
    if (argc == 4 && !strcmp(argv[1], "interpreter")) {
        bool passed = check(!strcmp(argv[0], "/system/bin/filesystem-child") &&
                            !strcmp(argv[2], "/data/local/tmp/exec-script"), "fs_script_arguments");
        passed &= check(contents("/system/etc/a10vm-marker", argv[3]), "fs_script_guest_paths");
        return passed ? 0 : 1;
    }
    if (argc != 3) return 64;
    if (!strcmp(argv[1], "owner-profile-setup")) return ownershipProfileSetup();
    if (!strcmp(argv[1], "owner-cold-tsync")) return ownershipSandboxCheck(true);
    if (!strcmp(argv[1], "owner-cold-prctl")) return ownershipSandboxCheck(false);
    if (!strcmp(argv[1], "owner-filter") || !strcmp(argv[1], "owner-sandbox")) {
        bool coldLibrary = !strcmp(argv[1], "owner-sandbox");
        if (coldLibrary) {
            int image = open("/data/local/tmp/owner-library.so", O_RDONLY | O_CLOEXEC);
            if (image < 0 || fchown(image, 10234, 10235) != 0) return 1;
            close(image);
        }
        void* loaded = dlopen("/data/local/tmp/owner-library.so", RTLD_NOW | RTLD_LOCAL);
        int library = open("/system/fonts/Roboto-Regular.ttf", O_RDONLY | O_CLOEXEC);
        int owned = open("/data/local/tmp/owner-filter", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        struct stat info{};
        bool passed = check(library >= 0 && fstat(library, &info) == 0, "fs_x5_library_preloaded");
        passed &= check(loaded != nullptr, "fs_x5_linker_preloaded");
        passed &= check(owned >= 0 && fchown(owned, 1000, 1000) == 0 && fstat(owned, &info) == 0 &&
                        info.st_uid == 1000 && info.st_gid == 1000, "fs_owner_cache_initial");
        passed &= check(fchown(owned, 10189, 10189) == 0 && fstat(owned, &info) == 0 &&
                        info.st_uid == 10189 && info.st_gid == 10189, "fs_owner_cache_invalidated");
        for (int i = 0; i < 320; ++i) {
            char path[96];
            snprintf(path, sizeof(path), "/data/local/tmp/owner-cache-%d", i);
            int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
            if (fd < 0 || fstat(fd, &info) != 0) return 1;
            close(fd);
        }
        sock_filter instructions[] = {
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_fgetxattr, 0, 1),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        };
        sock_fprog filter{4, instructions};
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) return 1;
        int filtered = coldLibrary
            ? syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &filter)
            : prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter);
        if (filtered) return 1;
        if (coldLibrary) {
            int image = open("/data/local/tmp/owner-library.so", O_RDONLY | O_CLOEXEC);
            passed &= check(image >= 0 && fstat(image, &info) == 0 &&
                            info.st_uid == 10234 && info.st_gid == 10235, "fs_x5_first_library_fstat");
            if (image >= 0) close(image);
        }
        passed &= check(fstat(library, &info) == 0, "fs_x5_library_fstat");
        passed &= check(fstat(owned, &info) == 0 && info.st_uid == 10189 && info.st_gid == 10189,
                        "fs_x5_owned_fstat");
        void* reloaded = dlopen("/data/local/tmp/owner-library.so", RTLD_NOW | RTLD_LOCAL);
        passed &= check(reloaded != nullptr && reloaded == loaded, "fs_x5_linker_reopened");
        if (reloaded) dlclose(reloaded);
        if (loaded) dlclose(loaded);
        close(library);
        close(owned);
        return passed ? 0 : 1;
    }
    if (!strcmp(argv[1], "font-filter")) {
        sock_filter instructions[] = {
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_readlinkat, 0, 1),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        };
        sock_fprog filter{4, instructions};
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
                prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter)) return 1;
        char target[32];
        bool passed = check(syscall(__NR_readlinkat, AT_FDCWD, "/system", target, sizeof(target)) == -1 &&
                            errno == EPERM, "fs_font_readlink_blocked");
        passed &= check(contents("/system/etc/fonts.xml", argv[2]), "fs_font_config_seccomp");
        passed &= check(contents("/system/fonts/Roboto-Regular.ttf", argv[2]), "fs_font_file_seccomp");
        return passed ? 0 : 1;
    }
    if (!strcmp(argv[1], "virtual-identity")) {
        bool passed = check(getuid() == 1000 && geteuid() == 1000 && getgid() == 1000 && getegid() == 1000,
                            "fs_virtual_identity");
        passed &= check(syscall(__NR_getuid) >= 10000 && syscall(__NR_geteuid) == syscall(__NR_getuid),
                        "fs_identity_kernel_uid");
        char sdk[PROP_VALUE_MAX]{};
        __system_property_get("ro.build.version.sdk", sdk);
        passed &= check(!strcmp(sdk, "29"), "fs_virtual_identity_properties");
        int timer = timerfd_create(CLOCK_BOOTTIME_ALARM, TFD_CLOEXEC);
        struct itimerspec alarm{};
        alarm.it_value.tv_nsec = 1000000;
        pollfd waiting{timer, POLLIN, 0};
        uint64_t expirations = 0;
        passed &= check(timer >= 0 && timerfd_settime(timer, 0, &alarm, nullptr) == 0 &&
                        poll(&waiting, 1, 1000) == 1 && read(timer, &expirations, sizeof(expirations)) == sizeof(expirations) &&
                        expirations == 1, "fs_guest_alarm_timer");
        if (timer >= 0) close(timer);
        struct timezone timezone{};
        passed &= check(settimeofday(nullptr, &timezone) == 0, "fs_guest_kernel_timezone");
        return passed ? 0 : 1;
    }
    if (!strcmp(argv[1], "child")) return child(argv[0], argv[2]) ? 0 : 1;
    const char* nonce = argv[2];
    bool passed = check(getuid() >= 10000 && geteuid() == getuid(), "fs_ordinary_uid");
    int nativeFd = syscall(__NR_openat, AT_FDCWD, "/system/etc/a10vm-marker", O_RDONLY, 0);
    char nativeText[128]{};
    bool nativeRead = nativeFd >= 0 && read(nativeFd, nativeText, sizeof(nativeText) - 1) >= 0 &&
                      !strcmp(nativeText, nonce);
    if (nativeFd >= 0) close(nativeFd);
    passed &= check(nativeRead, "fs_syscall_open");
    passed &= check(syscall(__NR_mkdirat, AT_FDCWD, "/data/local/tmp/native-dir", 0700) == 0,
                    "fs_syscall_mkdir");
    passed &= check(syscall(__NR_renameat, AT_FDCWD, "/data/local/tmp/native-dir",
                           AT_FDCWD, "/data/local/tmp/native-renamed") == 0, "fs_syscall_rename");
    struct stat nativeInfo{};
    passed &= check(syscall(__NR_newfstatat, AT_FDCWD, "/data/local/tmp/native-renamed",
                           &nativeInfo, 0) == 0 && S_ISDIR(nativeInfo.st_mode), "fs_syscall_stat");
    passed &= check(syscall(__NR_unlinkat, AT_FDCWD, "/data/local/tmp/native-renamed", AT_REMOVEDIR) == 0,
                    "fs_syscall_unlink");
    char symlinkTarget[PATH_MAX]{};
    const char* absoluteTarget = "/system/etc/a10vm-marker";
    bool linkedAbsolute = symlink(absoluteTarget, "/data/local/tmp/created-absolute-link") == 0;
    passed &= check(linkedAbsolute && contents("/data/local/tmp/created-absolute-link", nonce) &&
                    readlink("/data/local/tmp/created-absolute-link", symlinkTarget, sizeof(symlinkTarget)) ==
                    static_cast<ssize_t>(strlen(absoluteTarget)) && !strcmp(symlinkTarget, absoluteTarget),
                    "fs_create_absolute_symlink");
    if (linkedAbsolute) unlink("/data/local/tmp/created-absolute-link");
    int linkDirectory = open("/data/local/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    bool linkedRelative = linkDirectory >= 0 && symlinkat("../../../system/etc/a10vm-marker", linkDirectory,
                                                        "created-relative-link") == 0;
    passed &= check(linkedRelative && contents("/data/local/tmp/created-relative-link", nonce),
                    "fs_create_symlinkat_dirfd");
    if (linkedRelative) unlinkat(linkDirectory, "created-relative-link", 0);
    if (linkDirectory >= 0) close(linkDirectory);
    bool linkedSyscall = syscall(__NR_symlinkat, absoluteTarget, AT_FDCWD,
                                 "/data/local/tmp/created-syscall-link") == 0;
    passed &= check(linkedSyscall && contents("/data/local/tmp/created-syscall-link", nonce),
                    "fs_syscall_symlinkat");
    if (linkedSyscall) unlink("/data/local/tmp/created-syscall-link");
    errno = 0;
    passed &= check(symlink("missing", "/data/local/tmp/missing-parent/link") == -1 && errno == ENOENT,
                    "fs_create_symlink_missing_parent");
    passed &= check(contents("/system/etc/a10vm-marker", nonce), "fs_absolute_open");
    passed &= check(contents("/system/etc/absolute-marker", nonce), "fs_absolute_symlink");
    passed &= check(contents("/system/etc/relative-marker", nonce), "fs_relative_symlink");
    passed &= check(contents("/apex/runtime/a10vm-marker", nonce), "fs_apex_symlink");
    passed &= check(contents("/system/etc/../etc/a10vm-marker", nonce), "fs_parent_component");
    passed &= check(access("/system/etc/a10vm-marker", R_OK) == 0, "fs_access");
    struct stat info{};
    passed &= check(contents("/config/a10vm-marker", nonce), "fs_config_absolute_open");
    int configFd = syscall(__NR_openat, AT_FDCWD, "/config/a10vm-marker", O_RDONLY, 0);
    char configText[128]{};
    passed &= check(configFd >= 0 && read(configFd, configText, sizeof(configText) - 1) >= 0 &&
                    !strcmp(configText, nonce), "fs_config_syscall_open");
    if (configFd >= 0) close(configFd);
    DIR* configDirectory = opendir("/config");
    bool configMarker = false;
    if (configDirectory) {
        while (dirent* entry = readdir(configDirectory))
            if (!strcmp(entry->d_name, "a10vm-marker")) configMarker = true;
        closedir(configDirectory);
    }
    passed &= check(configMarker, "fs_config_directory_enumeration");
    int configDirFd = open("/config", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    passed &= check(configDirFd >= 0 && contents("a10vm-marker", nonce, configDirFd),
                    "fs_config_relative_dirfd");
    if (configDirFd >= 0) close(configDirFd);
    errno = 0;
    passed &= check(stat("/config/sdcardfs", &info) == -1 && errno == ENOENT,
                    "fs_missing_kernel_config_no_host_directory");
    passed &= check(stat("/system/etc/a10vm-marker", &info) == 0 && S_ISREG(info.st_mode), "fs_stat");
    struct statvfs space{};
    int spaceFd = open("/system/etc/a10vm-marker", O_RDONLY | O_CLOEXEC);
    struct statvfs descriptorSpace{};
    passed &= check(spaceFd >= 0 && fstatvfs(spaceFd, &descriptorSpace) == 0 &&
                    statvfs("/system/etc/a10vm-marker", &space) == 0 && space.f_blocks == descriptorSpace.f_blocks &&
                    space.f_bsize == descriptorSpace.f_bsize, "fs_statvfs_guest");
    if (spaceFd >= 0) close(spaceFd);
    passed &= check(lstat("/system/etc/absolute-marker", &info) == 0 && S_ISLNK(info.st_mode),
                    "fs_lstat_symlink");
    char canonical[PATH_MAX];
    passed &= check(realpath("/system/bin/filesystem-child", canonical) &&
                    !strcmp(canonical, getenv("A10VM_TEST_EXECUTABLE")), "fs_realpath_host_executable");
    passed &= check(realpath("/system/etc/absolute-marker", canonical) &&
                    !strcmp(canonical, "/system/etc/a10vm-marker"), "fs_realpath_guest_symlink");
    errno = 0;
    passed &= check(!realpath("/system/etc/missing-realpath", canonical) && errno == ENOENT,
                    "fs_realpath_missing");
    char link[128] = {};
    ssize_t length = readlink("/system/etc/absolute-marker", link, sizeof(link) - 1);
    passed &= check(length > 0 && !strcmp(link, "/system/etc/a10vm-marker"), "fs_readlink");
    int linkedFd = open("/system/etc/a10vm-marker", O_RDONLY | O_CLOEXEC);
    char descriptor[64];
    snprintf(descriptor, sizeof(descriptor), "/proc/self/fd/%d", linkedFd);
    memset(link, 0, sizeof(link));
    const char* expectedLink = "/system/etc/a10vm-marker";
    length = readlink(descriptor, link, sizeof(link));
    passed &= check(linkedFd >= 0 && length == static_cast<ssize_t>(strlen(expectedLink)) &&
                    !memcmp(link, expectedLink, length) && contents(link, nonce), "fs_readlink_fd_roundtrip");
    char truncated[8];
    memset(truncated, '!', sizeof(truncated));
    length = readlink(descriptor, truncated, 4);
    passed &= check(length == 4 && !memcmp(truncated, "/sys", 4) && truncated[4] == '!',
                    "fs_readlink_fd_truncated");
    memset(link, 0, sizeof(link));
    length = syscall(__NR_readlinkat, AT_FDCWD, descriptor, link, sizeof(link));
    passed &= check(length == static_cast<ssize_t>(strlen(expectedLink)) &&
                    !memcmp(link, expectedLink, length), "fs_syscall_readlink_fd");
    int descriptors = open("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    memset(link, 0, sizeof(link));
    length = readlinkat(descriptors, strrchr(descriptor, '/') + 1, link, sizeof(link));
    passed &= check(descriptors >= 0 && length == static_cast<ssize_t>(strlen(expectedLink)) &&
                    !memcmp(link, expectedLink, length), "fs_readlink_fd_relative");
    if (descriptors >= 0) close(descriptors);
    errno = 0;
    passed &= check(readlink(descriptor, link, 0) == -1 && errno == EINVAL, "fs_readlink_zero_size");
    errno = 0;
    passed &= check(readlink(descriptor, nullptr, sizeof(link)) == -1 && errno == EFAULT,
                    "fs_readlink_null_buffer");
    if (linkedFd >= 0) close(linkedFd);
    memset(canonical, 0, sizeof(canonical));
    length = readlink("/system/bin/filesystem-child", canonical, sizeof(canonical));
    passed &= check(length == static_cast<ssize_t>(strlen(getenv("A10VM_TEST_EXECUTABLE"))) &&
                    !strcmp(canonical, getenv("A10VM_TEST_EXECUTABLE")), "fs_readlink_host_target");
    errno = 0;
    int absent = open("/system/etc/hosts", O_RDONLY | O_CLOEXEC);
    passed &= check(absent < 0 && errno == ENOENT, "fs_missing_guest_no_host_file");
    if (absent >= 0) close(absent);
    errno = 0;
    int loop = open("/system/etc/loop-a", O_RDONLY | O_CLOEXEC);
    passed &= check(loop < 0 && errno == ELOOP, "fs_symlink_loop");
    if (loop >= 0) close(loop);
    int directory = open("/system/etc", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    passed &= check(directory >= 0 && contents("a10vm-marker", nonce, directory), "fs_openat_dirfd");
    passed &= check(directory >= 0 && contents("absolute-marker", nonce, directory),
                    "fs_openat_absolute_symlink");
    if (directory >= 0) close(directory);
    bool changed = chdir("/data/local/tmp") == 0;
    passed &= check(changed, "fs_chdir");
    char physical[PATH_MAX] = {};
    register long rawResult asm("x0") = AT_FDCWD;
    register const char* rawPath asm("x1") = "/proc/self/cwd";
    register char* rawBuffer asm("x2") = physical;
    register size_t rawSize asm("x3") = sizeof(physical) - 1;
    register long rawNumber asm("x8") = __NR_readlinkat;
    asm volatile("svc #0" : "+r"(rawResult) : "r"(rawPath), "r"(rawBuffer), "r"(rawSize),
                 "r"(rawNumber) : "memory", "cc");
    ssize_t physicalSize = rawResult;
    const char* root = getenv("A10VM_ROOTFS");
    bool privateCwd = physicalSize > 0 && root && !strncmp(physical, root, strlen(root)) &&
                      physical[strlen(root)] == '/';
    passed &= check(privateCwd, "fs_physical_cwd");
    memset(link, 0, sizeof(link));
    length = readlink("/proc/self/cwd", link, sizeof(link));
    passed &= check(length == 15 && !strcmp(link, "/data/local/tmp"), "fs_readlink_guest_cwd");
    char cwd[128];
    passed &= check(changed && getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, "/data/local/tmp"),
                    "fs_getcwd");
    passed &= check(changed && contents("../../../system/etc/a10vm-marker", nonce),
                    "fs_relative_from_cwd");
    if (changed && privateCwd) {
        int notifications = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        bool watching = notifications >= 0 && inotify_add_watch(notifications, "/data/local/tmp", IN_CREATE) >= 0;
        int fd = open("written", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        bool written = fd >= 0 && write(fd, nonce, strlen(nonce)) == static_cast<ssize_t>(strlen(nonce));
        if (fd >= 0) close(fd);
        passed &= check(written && contents("/data/local/tmp/written", nonce), "fs_private_write");
        pollfd notification{notifications, POLLIN, 0};
        char events[1024]{};
        passed &= check(watching && poll(&notification, 1, 1000) == 1 && read(notifications, events, sizeof(events)) > 0 &&
                        (reinterpret_cast<inotify_event*>(events)->mask & IN_CREATE), "fs_guest_inotify");
        if (notifications >= 0) close(notifications);
        int resized = open("download.midownload", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        const char payload[] = "APK-BYTES";
        bool seeded = resized >= 0 && write(resized, payload, sizeof(payload)) == sizeof(payload) &&
                      write(resized, "download-metadata", 17) == 17;
        passed &= check(seeded && truncate("/data/local/tmp/download.midownload", sizeof(payload)) == 0 &&
                        fstat(resized, &info) == 0 && info.st_size == sizeof(payload), "fs_download_truncate");
        passed &= check(seeded && truncate64("/data/local/tmp/download.midownload", sizeof(payload) + 8) == 0 &&
                        fstat(resized, &info) == 0 && info.st_size == sizeof(payload) + 8,
                        "fs_download_truncate64");
        passed &= check(seeded && syscall(__NR_truncate, "/data/local/tmp/download.midownload", sizeof(payload)) == 0 &&
                        contents("download.midownload", payload), "fs_download_syscall_truncate");
        errno = 0;
        passed &= check(truncate("/data/local/tmp/missing-download", 0) == -1 && errno == ENOENT,
                        "fs_download_truncate_missing");
        if (resized >= 0) close(resized);
        unlink("download.midownload");
        passed &= check(chmod("/data/local/tmp/written", 0640) == 0 &&
                        stat("written", &info) == 0 && (info.st_mode & 0777) == 0640, "fs_guest_chmod");
        passed &= check(chown("/data/local/tmp/written", 1000, 1000) == 0 &&
                        stat("written", &info) == 0 && info.st_uid == 1000 && info.st_gid == 1000,
                        "fs_guest_ownership");
        fd = open("written", O_RDONLY | O_CLOEXEC);
        passed &= check(fd >= 0 && fstat(fd, &info) == 0 && info.st_uid == 1000 &&
                        info.st_gid == 1000, "fs_guest_fstat_owner");
        passed &= check(fd >= 0 && syscall(__NR_fstat, fd, &info) == 0 &&
                        info.st_uid == getuid(), "fs_kernel_owner_unchanged");
        timeval modified[2]{{1700000000, 123456}, {1700000010, 654321}};
        passed &= check(utimes("/data/local/tmp/written", modified) == 0 &&
                        stat("written", &info) == 0 && info.st_mtim.tv_sec == modified[1].tv_sec &&
                        info.st_mtim.tv_nsec == modified[1].tv_usec * 1000, "fs_guest_utimes");
        timespec timestamps[2]{{1700000020, 123456789}, {1700000030, 987654321}};
        passed &= check(fd >= 0 && futimens(fd, timestamps) == 0 &&
                        fstat(fd, &info) == 0 && info.st_mtim.tv_sec == timestamps[1].tv_sec &&
                        info.st_mtim.tv_nsec == timestamps[1].tv_nsec, "fs_guest_futimens");
        directory = open("/data/local/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        timestamps[1].tv_sec++;
        passed &= check(directory >= 0 && utimensat(directory, "written", timestamps, 0) == 0 &&
                        stat("written", &info) == 0 && info.st_mtim.tv_sec == timestamps[1].tv_sec,
                        "fs_guest_utimensat_dirfd");
        if (directory >= 0) close(directory);
        bool linked = symlink("written", "timestamp-link") == 0;
        timestamps[1].tv_sec++;
        passed &= check(linked && utimensat(AT_FDCWD, "/data/local/tmp/timestamp-link", timestamps,
                        AT_SYMLINK_NOFOLLOW) == 0 && lstat("timestamp-link", &info) == 0 &&
                        info.st_mtim.tv_sec == timestamps[1].tv_sec && stat("written", &info) == 0 &&
                        info.st_mtim.tv_sec == timestamps[1].tv_sec - 1, "fs_guest_utimensat_symlink");
        if (linked) unlink("timestamp-link");
        errno = 0;
        passed &= check(utimensat(AT_FDCWD, "/data/local/tmp/missing-timestamp", timestamps, 0) == -1 &&
                        errno == ENOENT, "fs_guest_utimensat_missing");
        uint64_t inode = 42, recorded = 0;
        passed &= check(setxattr("/data/local/tmp/written", "user.inode_cache", &inode, sizeof(inode), 0) == 0,
                        "fs_guest_xattr_write");
        passed &= check(getxattr("/data/local/tmp/written", "user.inode_cache", &recorded, sizeof(recorded)) == sizeof(recorded) &&
                        recorded == inode, "fs_guest_xattr_read");
        recorded = 0;
        passed &= check(fd >= 0 && fgetxattr(fd, "user.inode_cache", &recorded, sizeof(recorded)) == sizeof(recorded) &&
                        recorded == inode, "fs_guest_xattr_descriptor");
        passed &= check(removexattr("/data/local/tmp/written", "user.inode_cache") == 0,
                        "fs_guest_xattr_remove");
        if (fd >= 0) close(fd);
        passed &= check(rename("written", "/data/local/tmp/renamed") == 0 &&
                        contents("renamed", nonce), "fs_rename");
        passed &= check(stat("renamed", &info) == 0 && info.st_uid == 1000 &&
                        info.st_gid == 1000, "fs_guest_owner_after_rename");
        passed &= check(unlink("renamed") == 0, "fs_unlink");
        passed &= check(mkdir("created", 0700) == 0 && rmdir("created") == 0, "fs_mkdir_rmdir");
        passed &= check(mkdir("/data/local/tmp/keva-slash/", 0700) == 0, "fs_mkdir_trailing_slash");
        int repository = open("/data/local/tmp/keva-slash", O_RDONLY | O_DIRECTORY);
        passed &= check(repository >= 0 && mkdirat(repository, "repo///", 0700) == 0,
                        "fs_mkdirat_trailing_slashes");
        passed &= check(syscall(__NR_mkdirat, AT_FDCWD, "/data/local/tmp/keva-slash/global/", 0700) == 0,
                        "fs_syscall_mkdir_trailing_slash");
        errno = 0;
        passed &= check(mkdir("/data/local/tmp/keva-slash/", 0700) == -1 && errno == EEXIST,
                        "fs_mkdir_trailing_existing");
        errno = 0;
        passed &= check(mkdir("/data/local/tmp/absent-parent/child/", 0700) == -1 && errno == ENOENT,
                        "fs_mkdir_trailing_missing_parent");
        int marker = openat(repository, "repo/agreement", O_CREAT | O_WRONLY, 0600);
        bool stored = marker >= 0 && write(marker, nonce, strlen(nonce)) == static_cast<ssize_t>(strlen(nonce)) &&
                      fsync(marker) == 0;
        if (marker >= 0) close(marker);
        passed &= check(stored && contents("/data/local/tmp/keva-slash/repo/agreement", nonce),
                        "fs_trailing_directory_persistent_write");
        errno = 0;
        passed &= check(mkdirat(repository, "repo/agreement/", 0700) == -1 && errno == EEXIST,
                        "fs_mkdir_trailing_regular_file");
        bool repositoryLinked = repository >= 0 && symlinkat("missing-target", repository, "existing-link") == 0;
        errno = 0;
        passed &= check(repositoryLinked && mkdirat(repository, "existing-link/", 0700) == -1 && errno == EEXIST,
                        "fs_mkdir_trailing_symlink");
        if (repository >= 0) close(repository);
        int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        strcpy(address.sun_path, "/dev/socket/fs-probe");
        bool connected = listener >= 0 && client >= 0 &&
                         bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
                         listen(listener, 1) == 0 &&
                         connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
        if (!connected) emit("filesystem_socket_errno=%d\n", errno);
        int peer = connected ? accept4(listener, nullptr, nullptr, SOCK_CLOEXEC) : -1;
        char received = 0;
        passed &= check(peer >= 0 && write(client, "A", 1) == 1 && read(peer, &received, 1) == 1 && received == 'A',
                        "fs_guest_socket_roundtrip");
        if (peer >= 0) close(peer);
        if (client >= 0) close(client);
        if (listener >= 0) close(listener);
        unlink("/dev/socket/fs-probe");
    }
    pid_t pid = fork();
    if (pid == 0) {
        char* const args[] = {const_cast<char*>("filesystem-child"), const_cast<char*>("child"),
                             const_cast<char*>(nonce), nullptr};
        execve("/system/bin/filesystem-child", args, environ);
        emit("filesystem_child_exec_errno=%d\n", errno);
        _exit(127);
    }
    int status = 0;
    pid_t waited = pid > 0 ? TEMP_FAILURE_RETRY(waitpid(pid, &status, 0)) : -1;
    passed &= check(waited == pid && pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                    "fs_guest_exec_chain");
    pid = fork();
    if (pid == 0) {
        char* const args[] = {const_cast<char*>("exec-script"), const_cast<char*>(nonce), nullptr};
        execve("/data/local/tmp/exec-script", args, environ);
        emit("filesystem_script_exec_errno=%d\n", errno);
        _exit(127);
    }
    waited = pid > 0 ? TEMP_FAILURE_RETRY(waitpid(pid, &status, 0)) : -1;
    passed &= check(waited == pid && pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
                    "fs_guest_script_exec");
    char* const loopArgs[] = {const_cast<char*>("exec-loop"), nullptr};
    errno = 0;
    passed &= check(execve("/data/local/tmp/exec-loop", loopArgs, environ) == -1 && errno == ELOOP,
                    "fs_guest_script_loop");
    errno = 0;
    char* const missingArgs[] = {const_cast<char*>("missing"), nullptr};
    passed &= check(execve("/system/bin/a10vm-missing", missingArgs, environ) == -1 && errno == ENOENT,
                    "fs_missing_exec");
    passed &= identity("parent", nonce);
    emit("filesystem_complete=%d\n", passed);
    return passed ? 0 : 1;
}
