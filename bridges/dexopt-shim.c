/*
 * Android 9 guest AOT escape hatch.
 *
 * The Redroid 9 arm64 image ships only a 32-bit /system/bin/dex2oat
 * (ELF class 1, EM_ARM).  The API 28 runtime here is 64-bit only, and guest
 * paths are virtual, so every installd `DexInv` ends with
 *
 *     Dex2oat invocation for ... failed with 0x001f (31)  // process failed
 *
 * PackageManagerService retries that for all ~74 system packages on *every*
 * boot, then BackgroundDexOptService keeps retrying in the background.  That
 * is the multi-tens-of-seconds "fresh boot is almost unusable" stall, and it
 * can never succeed on this image.
 *
 * With A10VM_SKIP_DEX2OAT=1 (A9 runtime profile only) the child that installd
 * forks for dex2oat exits 0 immediately, so installd reports success and
 * PackageManagerService stops retrying.  No AOT artefact is produced, so the
 * guest stays on interpreter + JIT - which is exactly what it already is, just
 * without paying for a doomed fork/exec on every boot.
 *
 * Do not enable this when a real 64-bit dex2oat exists.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int (*ExecFn)(const char*, char* const[]);
typedef int (*ExecVeFn)(const char*, char* const[], char* const[]);
/* AOSP 9 liblogwrap/fork_exec.h (Android 10 adds a trailing bool logwrap). */
typedef void (*LogFunction)(const char* format, ...);
typedef int (*ForkExecFn)(int argc, char* argv[], int* status, bool ignore_int_quit,
                          int logwrap_fd, LogFunction log_function);

static int skipDex2Oat(const char* path) {
    const char* enabled = getenv("A10VM_SKIP_DEX2OAT");
    return enabled != NULL && enabled[0] == '1' && path != NULL &&
           strstr(path, "dex2oat") != NULL;
}

static void say(const char* text) {
    size_t length = strlen(text);
    (void)!write(2, text, length);
}

__attribute__((constructor)) static void load(void) {
    const char* enabled = getenv("A10VM_SKIP_DEX2OAT");
    say("a9vm_dexopt_shim=loaded\n");
    say(enabled != NULL && enabled[0] == '1' ? "a9vm_dexopt_shim_skip=1\n"
                                             : "a9vm_dexopt_shim_skip=0\n");
}

int execv(const char* path, char* const argv[]) {
    if (skipDex2Oat(path)) { say("a9vm_dexopt_shim=skip_execv\n"); _exit(0); }
    static ExecFn real;
    if (real == NULL) real = (ExecFn)dlsym(RTLD_NEXT, "execv");
    if (real == NULL) return -1;
    return real(path, argv);
}

int execve(const char* path, char* const argv[], char* const envp[]) {
    if (skipDex2Oat(path)) { say("a9vm_dexopt_shim=skip_execve\n"); _exit(0); }
    static ExecVeFn real;
    if (real == NULL) real = (ExecVeFn)dlsym(RTLD_NEXT, "execve");
    if (real == NULL) return -1;
    return real(path, argv, envp);
}

/*
 * installd does not call execv()/execve() for dex2oat.  It calls
 * liblogwrap's android_fork_execvp_ext(), whose child path is:
 *
 *     fork() -> execvp(argv[0], argv)
 *
 * The two wrappers above therefore never fired, which is why
 * A10VM_SKIP_DEX2OAT still produced 74 "failed with 0x001f" lines per boot.
 * execvp() is the symbol that actually needs the escape hatch.
 */
int execvp(const char* file, char* const argv[]) {
    if (skipDex2Oat(file)) { say("a9vm_dexopt_shim=skip_execvp\n"); _exit(0); }
    /* argv[0] is what liblogwrap passes; cover the case where it differs. */
    if (argv != NULL && skipDex2Oat(argv[0])) { say("a9vm_dexopt_shim=skip_execvp\n"); _exit(0); }
    static ExecFn real;
    if (real == NULL) real = (ExecFn)dlsym(RTLD_NEXT, "execvp");
    if (real == NULL) return -1;
    return real(file, argv);
}

int execvpe(const char* file, char* const argv[], char* const envp[]) {
    if (skipDex2Oat(file)) { say("a9vm_dexopt_shim=skip_execvpe\n"); _exit(0); }
    if (argv != NULL && skipDex2Oat(argv[0])) { say("a9vm_dexopt_shim=skip_execvpe\n"); _exit(0); }
    static ExecVeFn real;
    if (real == NULL) real = (ExecVeFn)dlsym(RTLD_NEXT, "execvpe");
    if (real == NULL) return -1;
    return real(file, argv, envp);
}

/*
 * The execvp() wrapper above is loaded, but on this runtime the linker still
 * resolves liblogwrap's execvp to libc before the preloaded object (the shim
 * is in the global group, but libc precedes it for the same symbol name).
 * So intercept the liblogwrap entry point itself: it is not a libc symbol, it
 * is exported by liblogwrap.so, and installd imports it directly:
 *
 *     installd -> UND android_fork_execvp_ext -> liblogwrap android_fork_execvp_ext
 *
 * Returning 0 with status=0 is exactly what DexInv sees from a successful
 * dex2oat child; no fork happens, so there is nothing to wait for.
 */
int android_fork_execvp_ext(int argc, char* argv[], int* status, bool ignore_int_quit,
                            int logwrap_fd, LogFunction log_function) {
    if (argc > 0 && skipDex2Oat(argv[0])) {
        say("a9vm_dexopt_shim=skip_fork_execvp\n");
        if (status != NULL) *status = 0;
        return 0;
    }
    static ForkExecFn real;
    if (real == NULL) real = (ForkExecFn)dlsym(RTLD_NEXT, "android_fork_execvp_ext");
    if (real == NULL) return -1;
    return real(argc, argv, status, ignore_int_quit, logwrap_fd, log_function);
}
