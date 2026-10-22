#include <stddef.h>
#include <stdint.h>

/* Android 9 libandroid_runtime still references bionic's malloc-debug hooks. */
int gMallocLeakZygoteChild = 0;

void get_malloc_leak_info(uint8_t** info, size_t* overall_size, size_t* info_size,
                          size_t* total_memory, size_t* backtrace_size) {
    if (info) *info = NULL;
    if (overall_size) *overall_size = 0;
    if (info_size) *info_size = 0;
    if (total_memory) *total_memory = 0;
    if (backtrace_size) *backtrace_size = 0;
}

void free_malloc_leak_info(uint8_t* info) {
    (void) info;
}
