#include <processgroup/sched_policy.h>
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

extern "C" int a10vm_set_sched_policy(int tid, SchedPolicy policy) {
    if (policy != SP_FOREGROUND && policy != SP_BACKGROUND) {
        errno = ENOTSUP;
        return -1;
    }
    if (tid == 0) tid = gettid();
    char path[80];
    snprintf(path, sizeof(path), "/proc/self/task/%d", tid);
    if (access(path, F_OK) != 0) return -1;
    return setpriority(PRIO_PROCESS, tid, policy == SP_BACKGROUND ? 10 : 0);
}
