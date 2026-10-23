#!/bin/sh
# Build the AArch64 /system/bin/dex2oat replacement (see dex2oat-stub.S).
#
# The guest's libc execve wrapper (container-probe/runtime/filesystem/GuestExec.cpp)
# hands every ET_DYN target to the guest linker, and the linker needs a
# PT_INTERP pointing at itself.  A static PIE has no PT_INTERP, so the linker
# rejects it and installd still reports 0x001f.  Build a dynamic PIE with
# /system/bin/linker64 as the interpreter and no NEEDED libraries: the linker
# loads it like any other guest executable and jumps to _start, which only
# issues exit_group(0).
set -eu
MODULE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NDK=${ANDROID_NDK_HOME:-"$HOME/Android/Sdk/ndk/29.0.14206865"}
CLANG="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
if [ ! -x "$CLANG" ]; then
    # Windows NDK layout, used by the main workspace.
    CLANG="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
fi
test -x "$CLANG"
OUT="$MODULE/bridges/bin"
OBJ="$MODULE/bridges/obj/dex2oat-stub"
mkdir -p "$OUT" "$OBJ"
"$CLANG" --target=aarch64-linux-android28 -fPIE -pie -nostdlib \
    -Wl,--dynamic-linker=/system/bin/linker64 -Wl,-e,_start -Wl,--build-id=none \
    -o "$OBJ/dex2oat-stub" "$MODULE/bridges/dex2oat-stub.S"
llvm-readelf -hl "$OBJ/dex2oat-stub" > "$OBJ/dex2oat-stub.elf.txt" 2>/dev/null || true
cp "$OBJ/dex2oat-stub" "$OUT/dex2oat-stub"
chmod 755 "$OUT/dex2oat-stub"
echo "dex2oat-stub: $OUT/dex2oat-stub ($(wc -c < "$OUT/dex2oat-stub") bytes)"
