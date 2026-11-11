#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/container-probe/binder"
OUT="$ROOT/artifacts/container/runtime"
UP="$OUT/upstream"
B="$UP/bionic/libc"
CACHE="$ROOT/container-probe/runtime/dynamic/compile-source.py"
MEDIA="$ROOT/artifacts/container/media"
DEST="$ROOT/artifacts/device-compat/mapper"
unset LD_LIBRARY_PATH LD_PRELOAD
. "$ROOT/container-probe/binder/compile.sh"
mkdir -p "$DEST"
platform_compile "$ROOT/container-probe/graphics/desktop/PassthroughMapperCheck.cpp" "$DEST/check.o" \
    -I"$MEDIA/generated/include" -I"$UP/system/libhidl/base/include" -I"$UP/system/libhidl/transport/include"
ld.lld -pie -e _start -dynamic-linker /system/bin/linker64 -z now -z relro -z max-page-size=4096 \
    --no-undefined --as-needed "$OUT/obj/binder-probe/crtbegin.o" "$DEST/check.o" \
    "$ROOT/artifacts/container/framework/lib/libvndksupport.so" \
    "$ROOT/artifacts/container/framework/lib/libhidlbase.so" "$OUT/lib/libutils.so" \
    "$OUT/lib/libc++.so" "$OUT/lib/libunwind.so" "$OUT/lib/libc.so" "$OUT/lib/libdl.so" \
    "$OUT/bin/source-linker64" "$(clang --target=aarch64-linux-android29 --print-libgcc-file-name)" \
    "$OUT/obj/binder-probe/crtend.o" -o "$DEST/check"
