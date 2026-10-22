#include <pthread.h>

static __thread int tls_value = 29;
static int* tls_destructors;
static int* unloads;
static int initializers;

struct ThreadValue {
    int value = 29;
    ~ThreadValue() {
        if (value == 30 && tls_destructors) __atomic_add_fetch(tls_destructors, 1, __ATOMIC_SEQ_CST);
    }
};

__attribute__((constructor)) static void initialize() { ++initializers; }
__attribute__((destructor)) static void finalize() {
    if (unloads) __atomic_add_fetch(unloads, 1, __ATOMIC_SEQ_CST);
}

extern "C" int tls_probe_initializers() { return initializers; }
extern "C" void tls_probe_observe(int* destructors, int* module_unloads) {
    tls_destructors = destructors;
    unloads = module_unloads;
}
extern "C" int tls_probe_set(int value) {
    int previous = tls_value;
    tls_value = value;
    return previous;
}
extern "C" int* tls_probe_address() { return &tls_value; }
extern "C" bool tls_probe_touch() {
    static thread_local ThreadValue object;
    bool fresh = tls_value == 29 && object.value == 29;
    tls_value = 30;
    object.value = 30;
    return fresh;
}
