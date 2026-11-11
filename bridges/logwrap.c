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

/*
 * 永久追踪（与 GuestExec 的 a9exec.log 同一套思路，`docs/android9-port/28` §3.2）：
 * 只回答一件事——**installd 到底有没有走到这个入口**。
 *
 * 已知事实：`liba9vm-logwrap.so` 被 installd 加载（/proc/<pid>/maps 有 4 段），
 * 且全系统只有它导出 android_fork_execvp_ext；但 0x001f 依旧每轮出现。
 * 这条记录给出第三种可能：调用根本没到这里。
 *
 * 写入 <A10VM_STATE_ROOT>/a9logwrap.log，一行一次调用：
 *   enter <argv[0]> argc=<n>  进来的每一次调用
 *   skip dex2oat <argv[0]>    被跳过（返回 status=0）
 *   forkfail errno=<n>        fork 失败
 *   execfail <path> errno=<n> 子进程 execvp 失败
 */
static void trace_line(const char* prefix, const char* detail, long number) {
    const char* state = getenv("A10VM_STATE_ROOT");
    if (state == NULL || state[0] == '\0') return;
    char path[512];
    size_t used = strlen(state);
    if (used + 14 >= sizeof(path)) return;
    memcpy(path, state, used);
    memcpy(path + used, "/a9logwrap.log", 15);

    char line[768];
    size_t length = 0;
    size_t prefix_length = strlen(prefix);
    if (prefix_length > sizeof(line) - 64) return;
    memcpy(line, prefix, prefix_length);
    length = prefix_length;
    if (detail != NULL) {
        size_t detail_length = strlen(detail);
        if (detail_length > sizeof(line) - length - 40) detail_length = sizeof(line) - length - 40;
        memcpy(line + length, detail, detail_length);
        length += detail_length;
    }
    if (number >= 0) {
        char tail[32];
        int tail_length = 0;
        unsigned long value = (unsigned long) number;
        char digits[24];
        int count = 0;
        do { digits[count++] = (char) ('0' + (value % 10)); value /= 10; } while (value != 0 && count < 20);
        tail[tail_length++] = ' ';
        while (count > 0) tail[tail_length++] = digits[--count];
        tail[tail_length++] = '\n';
        memcpy(line + length, tail, (size_t) tail_length);
        length += (size_t) tail_length;
    } else {
        line[length++] = '\n';
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) return;
    ssize_t written = write(fd, line, length);
    (void) written;
    close(fd);
}

int android_fork_execvp_ext(int argc, char* argv[], int* status, bool ignore_int_quit,
                            int log_target, bool abbreviated) {
    (void)ignore_int_quit;
    (void)log_target;
    (void)abbreviated;
    trace_line("enter ", (argc > 0 && argv != NULL) ? argv[0] : "(none)", argc);
    if (argc > 0 && argv != NULL && is_dex2oat(argv[0])) {
        /* No 64-bit dex2oat exists on this image; report a successful child so
         * PackageManagerService stops replaying the whole system package list. */
        trace_line("skip dex2oat ", argv[0], -1);
        if (status != NULL) *status = 0;
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        trace_line("forkfail errno=", strerror(errno), -1);
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
        trace_line("execfail ", argv[0], errno);
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
