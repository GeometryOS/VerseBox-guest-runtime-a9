#include "GuestPaths.h"
#include <errno.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <unistd.h>

int timerfd_create(int clock, int flags) {
    if (guestRoot()) {
        if (clock == CLOCK_REALTIME_ALARM) clock = CLOCK_REALTIME;
        if (clock == CLOCK_BOOTTIME_ALARM) clock = CLOCK_BOOTTIME;
    }
    return syscall(__NR_timerfd_create, clock, flags);
}

int settimeofday(const struct timeval* time, const struct timezone* timezone) {
    if (guestRoot()) {
        // Android stores its timezone in properties; the host kernel timezone is unrelated.
        if (!time && timezone) return 0;
        errno = time ? EPERM : EINVAL;
        return -1;
    }
    return syscall(__NR_settimeofday, time, timezone);
}
