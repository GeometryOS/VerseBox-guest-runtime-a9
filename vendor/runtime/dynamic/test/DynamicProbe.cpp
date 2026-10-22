#include "../../test/ProbeOutput.h"
#include <dlfcn.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/system_properties.h>
#include "private/bionic_globals.h"

extern "C" libc_shared_globals* __loader_shared_globals();
int main(int, char**);
bool allocatorChecks();
bool loadingChecks(const char* module);

static char preinit_sdk[PROP_VALUE_MAX];
static char constructor_sdk[PROP_VALUE_MAX];
static __thread int initial_tls = 29;
static int order;

static void preinit() {
    __system_property_get("ro.build.version.sdk", preinit_sdk);
    if (initial_tls == 29) order = 1;
}
__attribute__((section(".preinit_array"), used)) static void (*preinit_entry)() = preinit;

__attribute__((constructor)) static void initialize() {
    __system_property_get("ro.build.version.sdk", constructor_sdk);
    if (order == 1) order = 2;
}

__attribute__((destructor)) static void finalize() {
    emit("dynamic_fini_complete=%d\n", order == 3);
}

static bool execChild(const char* mode, const char* nonce) {
    const char* expected = strcmp(mode, "reader") == 0 ? "29" : "";
    char sdk[PROP_VALUE_MAX] = {};
    __system_property_get("ro.build.version.sdk", sdk);
    bool passed = check(order == 2 && preinit_sdk[0] == '\0' &&
                        !strcmp(constructor_sdk, expected) && !strcmp(sdk, expected),
                        "dynamic_exec_child_properties");
    Dl_info loader{}, libc{};
    bool loader_found = dladdr(reinterpret_cast<void*>(&__loader_shared_globals), &loader);
    bool libc_found = dladdr(reinterpret_cast<void*>(&getpid), &libc);
    passed &= check(loader_found && libc_found && loader.dli_fname != nullptr &&
                    libc.dli_fname != nullptr && loader.dli_fbase != libc.dli_fbase,
                    "dynamic_exec_child_runtime");
    emit("dynamic_exec_child_started=1\ndynamic_exec_child_mode=%s\n"
         "dynamic_exec_child_sdk=%s\ndynamic_exec_child_linker=%s\n"
         "dynamic_exec_child_libc=%s\n", mode, sdk,
         loader_found ? loader.dli_fname : "", libc_found ? libc.dli_fname : "");
    passed &= printIdentity(nonce);
    order = 3;
    emit("dynamic_exec_child_complete=%d\n", passed);
    return passed;
}

static bool execGuestProgram(const char* mode, const char* nonce) {
    Dl_info loader{}, program{};
    if (!dladdr(reinterpret_cast<void*>(&__loader_shared_globals), &loader) ||
        !dladdr(reinterpret_cast<void*>(&main), &program) ||
        loader.dli_fname == nullptr || program.dli_fname == nullptr) {
        return check(false, "dynamic_exec_chain");
    }

    pid_t child = fork();
    if (child == 0) {
        char* const arguments[] = {
            const_cast<char*>(loader.dli_fname), const_cast<char*>(program.dli_fname),
            const_cast<char*>("exec-child"),
            const_cast<char*>(mode), const_cast<char*>(nonce), nullptr,
        };
        execve(loader.dli_fname, arguments, environ);
        _exit(127);
    }
    if (child < 0) return check(false, "dynamic_exec_chain");

    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    bool passed = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    emit("dynamic_exec_child_status=%d\n", passed ? 0 : -1);
    return check(passed, "dynamic_exec_chain");
}

int main(int argc, char** argv) {
    if (argc != 4) return 64;
    if (strcmp(argv[1], "exec-child") == 0) {
        return execChild(argv[2], argv[3]) ? 0 : 1;
    }
    pid_t parent = getppid();
    if (parent == 1 || prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) return 70;
    const char* expected = strcmp(argv[1], "reader") == 0 ? "29" : "";
    char sdk[PROP_VALUE_MAX] = {};
    __system_property_get("ro.build.version.sdk", sdk);
    emit("dynamic_mode=%s\npreinit_sdk=%s\nconstructor_sdk=%s\nmain_sdk=%s\n",
         argv[1], preinit_sdk, constructor_sdk, sdk);
    bool passed = check(order == 2 && preinit_sdk[0] == '\0' &&
                        !strcmp(constructor_sdk, expected) && !strcmp(sdk, expected),
                        "dynamic_property_init_order");
    libc_shared_globals* globals = __loader_shared_globals();
    passed &= check(globals && globals->initial_linker_arg_count == 1 && globals->auxv &&
                    globals->init_environ == environ && globals->tls_modules.generation_libc_so &&
                    atomic_load(globals->tls_modules.generation_libc_so) ==
                    atomic_load(&globals->tls_modules.generation), "dynamic_shared_globals");
    Dl_info loader{}, libc{};
    bool loader_found = dladdr(reinterpret_cast<void*>(&__loader_shared_globals), &loader);
    bool libc_found = dladdr(reinterpret_cast<void*>(&getpid), &libc);
    passed &= check(loader_found && libc_found && loader.dli_fbase != libc.dli_fbase,
                    "dynamic_loader_libc_distinct");
    emit("source_linker_found=%d\nsource_libc_found=%d\nsource_linker=%s\nsource_libc=%s\n",
         loader_found, libc_found, loader.dli_fname, libc.dli_fname);
    passed &= execGuestProgram(argv[1], argv[2]);
    passed &= threadChecks();
    passed &= allocatorChecks();
    passed &= loadingChecks(argv[3]);
    passed &= printIdentity(argv[2]);
    static char exit_buffer[128];
    passed &= check(setvbuf(stdout, exit_buffer, _IOFBF, sizeof(exit_buffer)) == 0 &&
                    fputs("dynamic_stdio_flush_complete=1\n", stdout) >= 0, "dynamic_stdio_buffered");
    order = 3;
    emit("dynamic_complete=%d\n", passed);
    return passed ? 0 : 1;
}
