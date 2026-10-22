#!/bin/sh
set -eu
PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SRC="$PROJECT/container-probe/runtime/dynamic"
OUT="$PROJECT/artifacts/container/runtime"
UP="$OUT/upstream"
B="$UP/bionic/libc"
unset LD_LIBRARY_PATH LD_PRELOAD
mkdir -p "$OUT/obj/filesystem" "$OUT/filesystem"
BUILD_KIND=shared
. "$SRC/compile-platform.sh"
compile "$PROJECT/container-probe/runtime/filesystem/test/FilesystemProbe.cpp" \
    "$OUT/obj/filesystem/FilesystemProbe.o" -DA10VM_DYNAMIC_PROBE
compile "$PROJECT/container-probe/runtime/filesystem/test/OwnershipSandboxCheck.cpp" \
    "$OUT/obj/filesystem/OwnershipSandboxCheck.o" -DA10VM_DYNAMIC_PROBE
compile "$PROJECT/container-probe/runtime/test/ProcessIdentity.cpp" \
    "$OUT/obj/filesystem/ProcessIdentity.o" -DA10VM_DYNAMIC_PROBE
builtins=$(clang --target=aarch64-linux-android29 --print-libgcc-file-name)
ld.lld -pie -e _start -dynamic-linker /system/bin/linker64 \
    -z max-page-size=4096 -z now -z relro --hash-style=gnu --eh-frame-hdr --build-id=sha1 \
    --no-undefined --exclude-libs ALL \
    "$OUT/obj/dynamic-probe/crtbegin.o" "$OUT/obj/filesystem/FilesystemProbe.o" \
    "$OUT/obj/filesystem/OwnershipSandboxCheck.o" \
    "$OUT/obj/filesystem/ProcessIdentity.o" "$OUT/lib/libc.so" "$OUT/lib/libdl.so" \
    "$OUT/bin/source-linker64" "$builtins" "$OUT/obj/dynamic-probe/crtend.o" \
    -o "$OUT/bin/filesystem-probe"
