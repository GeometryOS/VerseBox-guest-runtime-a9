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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int (*ExecFn)(const char*, char* const[]);
typedef int (*ExecVeFn)(const char*, char* const[], char* const[]);

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
