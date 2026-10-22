#include "../../test/ProbeOutput.h"
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

static pthread_barrier_t start;
static constexpr int kWorkers = 4;
static constexpr int kRounds = 64;

static bool clearChecks() {
    constexpr size_t capacity = 8192;
    auto* bytes = static_cast<unsigned char*>(malloc(capacity));
    if (!bytes) return false;
    void* (*volatile fill)(void*, int, size_t) = memset;
    const size_t sizes[] = {97, 128, 255, 256, 257, 512, 775, 776, 777, 1024, 4096};
    for (size_t size : sizes) {
        for (size_t alignment = 0; alignment < 256; ++alignment) {
            fill(bytes, 0xa5, capacity);
            size_t begin = 256 + alignment;
            fill(bytes + begin, 0, size);
            for (size_t offset = 0; offset < capacity; ++offset) {
                unsigned char expected = offset >= begin && offset < begin + size ? 0 : 0xa5;
                if (bytes[offset] != expected) {
                    emit("allocator_clear_failure_size=%zu\nalignment=%zu\noffset=%zu\nactual=%u\n",
                         size, alignment, offset, bytes[offset]);
                    free(bytes);
                    return false;
                }
            }
        }
    }
    free(bytes);
    return true;
}

static void* allocate(void* argument) {
    uintptr_t index = reinterpret_cast<uintptr_t>(argument);
    bool passed = clearChecks();
    pthread_barrier_wait(&start);
    for (int round = 0; round < kRounds; ++round) {
        size_t size = 17 + ((round * 113 + index * 29) % 4096);
        unsigned char pattern = static_cast<unsigned char>(round + index);
        auto* bytes = static_cast<unsigned char*>(malloc(size));
        if (!bytes) { passed = false; break; }
        memset(bytes, pattern, size);
        auto* grown = static_cast<unsigned char*>(realloc(bytes, size + 3073));
        if (!grown) { free(bytes); passed = false; break; }
        for (size_t offset = 0; offset < size; ++offset) passed &= grown[offset] == pattern;
        passed &= malloc_usable_size(grown) >= size + 3073;
        free(grown);
        void* aligned = nullptr;
        passed &= posix_memalign(&aligned, 256, size) == 0;
        if (!aligned) { passed = false; break; }
        passed &= reinterpret_cast<uintptr_t>(aligned) % 256 == 0;
        memset(aligned, pattern, size);
        free(aligned);
        auto* zeroed = static_cast<unsigned char*>(calloc(size, 1));
        if (!zeroed) { passed = false; break; }
        for (size_t offset = 0; offset < size; ++offset) passed &= zeroed[offset] == 0;
        free(zeroed);
    }
    return reinterpret_cast<void*>(static_cast<uintptr_t>(passed));
}

bool allocatorChecks() {
    if (!check(pthread_barrier_init(&start, nullptr, kWorkers + 1) == 0,
               "dynamic_allocator_barrier")) return false;
    pthread_t workers[kWorkers];
    for (uintptr_t index = 0; index < kWorkers; ++index) {
        if (pthread_create(&workers[index], nullptr, allocate, reinterpret_cast<void*>(index))) {
            emit("CHECK\tdynamic_allocator_create\t0\n");
            _exit(1);
        }
    }
    pthread_barrier_wait(&start);
    bool passed = true;
    for (auto worker : workers) {
        void* result = nullptr;
        passed &= pthread_join(worker, &result) == 0 && result == reinterpret_cast<void*>(1);
    }
    passed &= pthread_barrier_destroy(&start) == 0;
    emit("allocator_workers=%d\nallocator_rounds_per_worker=%d\n", kWorkers, kRounds);
    return check(passed, "dynamic_allocator_concurrent");
}
