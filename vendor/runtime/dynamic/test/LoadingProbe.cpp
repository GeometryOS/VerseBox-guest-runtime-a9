#include "../../test/ProbeOutput.h"
#include <dlfcn.h>
#include <android/dlext.h>
#include <limits.h>
#include <link.h>
#include <pthread.h>
#include "private/bionic_globals.h"

extern "C" libc_shared_globals* __loader_shared_globals();
extern "C" android_namespace_t* __loader_android_create_namespace(const char*, const char*, const char*,
    uint64_t, const char*, android_namespace_t*, const void*);
extern "C" bool __loader_android_link_namespaces(android_namespace_t*, android_namespace_t*, const char*);

struct Worker {
    pthread_barrier_t ready, loaded, touched, release;
    bool (*touch)() = nullptr;
    int* (*address)() = nullptr;
    int* tls_address = nullptr;
    bool passed = false;
};

static void* worker(void* argument) {
    auto* state = static_cast<Worker*>(argument);
    pthread_barrier_wait(&state->ready);
    pthread_barrier_wait(&state->loaded);
    if (state->touch && state->address) {
        state->passed = state->touch();
        state->tls_address = state->address();
    }
    pthread_barrier_wait(&state->touched);
    pthread_barrier_wait(&state->release);
    return nullptr;
}

struct Search { const char* name; bool found; };
static int search(dl_phdr_info* info, size_t, void* argument) {
    auto* state = static_cast<Search*>(argument);
    if (!strcmp(info->dlpi_name, state->name)) state->found = true;
    return 0;
}
static bool loaded(const char* name) {
    Search state{name, false};
    dl_iterate_phdr(search, &state);
    return state.found;
}

bool loadingChecks(const char* module) {
    bool passed = true;
    int destructors = 0, unloads = 0;
    auto* modules = &__loader_shared_globals()->tls_modules;
    for (int round = 0; round < 2; ++round) {
        Worker state;
        pthread_barrier_t* barriers[] = {&state.ready, &state.loaded, &state.touched, &state.release};
        for (auto* barrier : barriers) {
            if (pthread_barrier_init(barrier, nullptr, 2)) return false;
        }
        pthread_t thread;
        if (pthread_create(&thread, nullptr, worker, &state)) return false;
        pthread_barrier_wait(&state.ready);
        size_t generation = atomic_load(&modules->generation);
        void* handle = dlopen(module, RTLD_NOW | RTLD_LOCAL);
        passed &= check(handle != nullptr, "dynamic_dlopen");
        if (!handle) emit("dlerror=%s\n", dlerror());
        auto observe = reinterpret_cast<void (*)(int*, int*)>(handle ? dlsym(handle, "tls_probe_observe") : nullptr);
        auto set = reinterpret_cast<int (*)(int)>(handle ? dlsym(handle, "tls_probe_set") : nullptr);
        auto initializers = reinterpret_cast<int (*)()>(handle ? dlsym(handle, "tls_probe_initializers") : nullptr);
        state.touch = reinterpret_cast<bool (*)()>(handle ? dlsym(handle, "tls_probe_touch") : nullptr);
        state.address = reinterpret_cast<int* (*)()>(handle ? dlsym(handle, "tls_probe_address") : nullptr);
        bool symbols = observe && set && initializers && state.touch && state.address;
        passed &= check(symbols, "dynamic_dlsym");
        int* main_tls = nullptr;
        if (symbols) {
            observe(&destructors, &unloads);
            main_tls = state.address();
            passed &= check(initializers() == 1 && set(31) == 29 && set(31) == 31,
                            "dynamic_tls_main_initialization");
        }
        pthread_barrier_wait(&state.loaded);
        pthread_barrier_wait(&state.touched);
        passed &= check(symbols && state.passed && state.tls_address != main_tls && *main_tls == 31,
                        "dynamic_tls_existing_thread");
        passed &= check(atomic_load(&modules->generation) > generation &&
                        atomic_load(modules->generation_libc_so) == atomic_load(&modules->generation),
                        "dynamic_tls_generation");
        passed &= check(handle && dlclose(handle) == 0 && loaded(module) &&
                        destructors == round && unloads == round, "dynamic_tls_dso_pinned");
        pthread_barrier_wait(&state.release);
        passed &= check(pthread_join(thread, nullptr) == 0 && destructors == round + 1 &&
                        unloads == round + 1 && !loaded(module), "dynamic_tls_dso_unloaded");
        for (auto* barrier : barriers) passed &= pthread_barrier_destroy(barrier) == 0;
    }
    emit("dynamic_tls_rounds=2\ndynamic_tls_destructors=%d\ndynamic_module_unloads=%d\n",
         destructors, unloads);
    char directory[PATH_MAX], path[PATH_MAX];
    strlcpy(directory, module, sizeof(directory));
    char* slash = strrchr(directory, '/');
    if (!slash) return false;
    *slash = '\0';
    auto* ns = __loader_android_create_namespace("guest-classloader-test", directory, directory, 1,
        directory, nullptr, reinterpret_cast<const void*>(&loadingChecks));
    bool linked = ns && __loader_android_link_namespaces(ns, nullptr, "libc.so:libdl.so");
    passed &= check(linked, "dynamic_namespace_created");
    snprintf(path, sizeof(path), "%s/liba10vm-namespace-test.so", directory);
    android_dlextinfo extension{};
    extension.flags = ANDROID_DLEXT_USE_NAMESPACE;
    extension.library_namespace = ns;
    void* handle = linked ? android_dlopen_ext(path, RTLD_NOW | RTLD_LOCAL, &extension) : nullptr;
    auto shared = reinterpret_cast<libc_shared_globals* (*)()>(handle ? dlsym(handle, "namespace_shared_globals") : nullptr);
    passed &= check(shared && shared() == __loader_shared_globals(), "dynamic_namespace_shared_linker");
    if (!handle) emit("namespace_dlerror=%s\n", dlerror());
    passed &= check(handle && dlclose(handle) == 0 && !loaded(path), "dynamic_namespace_unloaded");
    return passed;
}
