#pragma once
#include <stdint.h>
extern "C" {
const char* a10binder_default_driver();
const char* a10binder_default_hwbinder_driver();
int a10binder_open(const char* path);
int a10binder_ioctl(int fd, unsigned long request, void* argument);
int a10binder_close(int fd);
int a10binder_is_driver(int fd);
struct a10binder_buffer_stats { uint64_t allocated, freed, active, bytes; };
void a10binder_get_buffer_stats(a10binder_buffer_stats* stats);
}
