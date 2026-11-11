#pragma once

#include <cstdio>
#include <cstdlib>

#define ALOGE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define ALOGW(...) ALOGE(__VA_ARGS__)
#define ALOGI(...) ALOGE(__VA_ARGS__)
#ifdef A10VM_GL_VERBOSE
#define ALOGD(...) ALOGE(__VA_ARGS__)
#define ALOGV(...) ALOGE(__VA_ARGS__)
#else
#define ALOGD(...) ((void)0)
#define ALOGV(...) ((void)0)
#endif
#define LOG_ALWAYS_FATAL(...) do { ALOGE(__VA_ARGS__); std::abort(); } while (0)
#define LOG_ALWAYS_FATAL_IF(condition, ...) do { if (condition) LOG_ALWAYS_FATAL(__VA_ARGS__); } while (0)
#define ALOG_ASSERT(condition, ...) LOG_ALWAYS_FATAL_IF(!(condition), __VA_ARGS__)
