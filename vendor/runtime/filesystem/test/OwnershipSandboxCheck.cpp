#include "../../test/ProbeOutput.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

namespace {
struct Reader {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
    bool ready = false;
    bool passed = false;
    bool filtered = false;
};

bool coldStat(const char* path) {
    struct stat info{};
    errno = EBUSY;
    return stat(path, &info) == 0 &&
        info.st_uid == 10246 && info.st_gid == 10247 && errno == EBUSY;
}

void* readProfile(void* value) {
    auto* reader = static_cast<Reader*>(value);
    pthread_mutex_lock(&reader->mutex);
    while (!reader->ready) pthread_cond_wait(&reader->condition, &reader->mutex);
    pthread_mutex_unlock(&reader->mutex);
    reader->passed = coldStat("/data/local/tmp/renderer_dex.jar.ref.prof");
    errno = 0;
    reader->filtered = syscall(__NR_getppid) == -1 && errno == EPERM;
    return nullptr;
}
}

int ownershipProfileSetup() {
    const char* paths[] = {"/data/local/tmp/renderer_dex.jar.cur.prof",
                           "/data/local/tmp/renderer_dex.jar.ref.prof"};
    for (const char* path : paths) {
        int profile = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        bool passed = profile >= 0 && fchown(profile, 10246, 10247) == 0;
        if (profile >= 0) close(profile);
        if (!passed) return 1;
    }
    return 0;
}

int ownershipSandboxCheck(bool tsync) {
    int library = open("/data/local/tmp/owner-library.so", O_RDONLY | O_CLOEXEC);
    struct stat info{};
    if (library < 0 || fstat(library, &info) != 0) return 1;
    uid_t uid = info.st_uid;
    gid_t gid = info.st_gid;
    Reader reader;
    pthread_t thread;
    if (pthread_create(&thread, nullptr, readProfile, &reader)) return 1;
    sock_filter instructions[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_getxattr, 1, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_fgetxattr, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_getppid, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    sock_fprog filter{7, instructions};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) return 1;
    int result = tsync
        ? syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &filter)
        : prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter);
    bool passed = check(result == 0, "fs_owner_filter_installed");
    if (result) return 1;
    passed &= check(coldStat("/data/local/tmp/renderer_dex.jar.cur.prof"), "fs_owner_cold_profile_stat");
    timespec times[] = {{123456, 0}, {123457, 0}};
    passed &= check(futimens(library, times) == 0 && fstat(library, &info) == 0 &&
                    info.st_uid == uid && info.st_gid == gid, "fs_owner_changed_ctime_fstat");
    pthread_mutex_lock(&reader.mutex);
    reader.ready = true;
    pthread_cond_signal(&reader.condition);
    pthread_mutex_unlock(&reader.mutex);
    pthread_join(thread, nullptr);
    passed &= check(reader.passed, "fs_owner_profile_thread_stat");
    passed &= check(reader.filtered == tsync, "fs_owner_existing_thread_filter");
    errno = 0;
    passed &= check(syscall(__NR_getppid) == -1 && errno == EPERM, "fs_owner_original_filter_errno");
    pid_t child = fork();
    if (child == 0) {
        char owner[16];
        syscall(__NR_fgetxattr, library, "user.a10vm.owner", owner, sizeof(owner));
        _exit(1);
    }
    int status = 0;
    passed &= check(child > 0 && waitpid(child, &status, 0) == child &&
                    WIFSIGNALED(status) && WTERMSIG(status) == SIGSYS, "fs_owner_direct_xattr_blocked");
    close(library);
    return passed ? 0 : 1;
}
