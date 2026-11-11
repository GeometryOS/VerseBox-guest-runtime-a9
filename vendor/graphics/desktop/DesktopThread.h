#pragma once
#include <pthread.h>
#include <sys/resource.h>
#include <android/log.h>
#include <cerrno>

namespace desktopthread {
inline void configure(const char* name, int priority = 0) {
    const int named = pthread_setname_np(pthread_self(), name);
    // Apply only to this app's thread. The host scheduler selects available cores.
    const int result = setpriority(PRIO_PROCESS, 0, priority);
    const int priorityError = result ? errno : 0;
    errno = 0;
    const int actual = getpriority(PRIO_PROCESS, 0);
    const int readError = errno;
    __android_log_print(named || priorityError || readError ? ANDROID_LOG_WARN : ANDROID_LOG_INFO,
        "a10vm", "desktop_thread=%s requested_nice=%d actual_nice=%d name_error=%d nice_error=%d read_error=%d",
        name, priority, actual, named, priorityError, readError);
}
}
