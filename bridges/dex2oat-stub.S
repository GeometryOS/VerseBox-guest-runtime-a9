/*
 * Minimal AArch64 replacement for /system/bin/dex2oat.
 *
 * The Redroid 9 arm64 image ships only a 32-bit dex2oat (ELF32 EM_ARM), but the
 * API 28 runtime in this project is 64-bit.  Every installd DexInv therefore
 * ends in "Dex2oat invocation ... failed with 0x001f (31)" and
 * PackageManagerService retries all ~74 system packages on every boot.
 *
 * The previous dexopt shim only intercepted execv/execve in a preloaded
 * library, but installd reaches the child through liblogwrap / posix_spawn, so
 * the interception never fired.  Replacing the image's dex2oat with a 64-bit
 * executable removes the guesswork: whichever exec path installd uses, the
 * kernel can load this file, it exits 0 immediately, and installd reports a
 * successful DexInv.
 *
 * This is a dynamic PIE whose PT_INTERP is /system/bin/linker64 and which has
 * no NEEDED libraries.  That is deliberate: the guest's libc execve wrapper
 * (GuestExec.cpp) hands every ET_DYN target to the guest linker, and the
 * linker needs the target's PT_INTERP to point at itself.  A static PIE has no
 * PT_INTERP, so the linker rejects it and installd still reports 0x001f.
 * With the interpreter set, the linker loads this file like any other guest
 * executable and jumps to _start, which only issues exit_group(0).
 *
 * It is an image-level shim, not an AOT compiler: no oat file is produced, so
 * the guest keeps running interpreter + JIT exactly as it already did.  The
 * only behaviour that changes is that the guest stops paying for a doomed
 * dex2oat on every boot.
 */
    .section .text
    .global _start
    .type _start, %function
_start:
    /* exit_group(0): x8 = __NR_exit_group (94 on arm64).  Using exit_group
     * makes the single-threaded child terminate deterministically even if the
     * loader ever starts more than one thread. */
    mov     x0, #0
    mov     x8, #94
    svc     #0
1:
    b       1b
    .size _start, .-_start
