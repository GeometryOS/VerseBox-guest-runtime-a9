#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int emit(int priority, const char* tag, const char* text) {
    int saved = errno;
    char line[4096];
    int length = snprintf(line, sizeof(line), "GUEST_LOG %d %s: %s\n", priority, tag ? tag : "", text ? text : "");
    if (length >= (int)sizeof(line)) {
        length = sizeof(line) - 1;
        line[length - 1] = '\n';
    }
    // stdout and stderr share a log file. Keep each native record in one write
    // so its prefix cannot split a Java startup identity record.
    if (length > 0) while (write(STDERR_FILENO, line, length) < 0 && errno == EINTR) { }
    errno = saved;
    return 1;
}

int __android_log_write(int priority, const char* tag, const char* text) {
    return emit(priority, tag, text);
}

int __android_log_buf_write(int buffer, int priority, const char* tag, const char* text) {
    (void) buffer;
    return __android_log_write(priority, tag, text);
}

int __android_log_vprint(int priority, const char* tag, const char* format, va_list args) {
    int saved = errno;
    char text[4096];
    vsnprintf(text, sizeof(text), format, args);
    errno = saved;
    return emit(priority, tag, text);
}

int __android_log_print(int priority, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = __android_log_vprint(priority, tag, format, args);
    va_end(args);
    return result;
}

int __android_log_buf_print(int buffer, int priority, const char* tag, const char* format, ...) {
    (void) buffer;
    va_list args;
    va_start(args, format);
    int result = __android_log_vprint(priority, tag, format, args);
    va_end(args);
    return result;
}

void __android_log_assert(const char* condition, const char* tag, const char* format, ...) {
    if (format) {
        va_list args;
        va_start(args, format);
        __android_log_vprint(7, tag, format, args);
        va_end(args);
    } else __android_log_write(7, tag, condition);
    abort();
}
