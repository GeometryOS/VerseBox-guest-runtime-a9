/*
 * Android 9 guest liblogwrap replacement.
 *
 * Why this exists: the ROM's /system/lib64/liblogwrap.so creates a pty
 * (open /dev/ptmx -> grantpt -> unlockpt -> ptsname_r) *before* fork().  In the
 * container the app process cannot use a pty, so every installd DexInv returns
 * 0x001f without ever forking or exec'ing dex2oat - which is why neither
 * GuestExec::execve (a9exec.log) nor the LD_PRELOAD dexopt-shim ever saw a
 * dex2oat call.  objdump of the ROM library shows the pty calls before the
 * fork branch.
 *
 * This library exports the same AOSP 9 entry point, skips dex2oat entirely,
 * and uses a plain fork + execvp + waitpid for every other helper.  It is
 * packaged as /system/lib64/liblogwrap.so for the A9 rootfs only.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static bool is_dex2oat(const char* path) {
    if (path == NULL) return false;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return strcmp(base, "dex2oat") == 0 || strcmp(base, "dex2oatd") == 0;
}

int android_fork_execvp_ext(int argc, char* argv[], int* status, bool ignore_int_quit,
                            int log_target, bool abbreviated) {
    (void)ignore_int_quit;
    (void)log_target;
    (void)abbreviated;
    if (argc > 0 && argv != NULL && is_dex2oat(argv[0])) {
        /* No 64-bit dex2oat exists on this image; report a successful child so
         * PackageManagerService stops replaying the whole system package list. */
        if (status != NULL) *status = 0;
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        if (status != NULL) *status = -1;
        return -1;
    }
    if (pid == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) close(null_fd);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int wait_status = 0;
    while (waitpid(pid, &wait_status, 0) < 0) {
        if (errno != EINTR) {
            if (status != NULL) *status = -1;
            return -1;
        }
    }
    if (status != NULL) {
        *status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 128;
    }
    return 0;
}
