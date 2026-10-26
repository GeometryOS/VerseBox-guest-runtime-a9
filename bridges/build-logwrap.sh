#!/bin/sh
# Build liba9vm-logwrap.so, packaged as the A9 rootfs' /system/lib64/liblogwrap.so.
# See logwrap.c for why the ROM library cannot be used inside the container.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
. "$(dirname -- "$0")/compile.sh"

OBJ="$OUT/obj/logwrap"
mkdir -p "$OBJ" "$OUT/lib"
test -f "$RT/build/lib/libc.so" || { echo "run build-libc.sh first" >&2; exit 1; }

crt_so
platform_compile "$BR/logwrap.c" "$OBJ/logwrap.o"

ld.lld -shared -soname liblogwrap.so -z now -z relro -z max-page-size=4096 \
    --no-undefined "$OUT/obj/crt/crtbegin_so.o" "$OBJ/logwrap.o" \
    "$LIBDL" "$LIBC" "$OUT/obj/crt/crtend_so.o" \
    -o "$OUT/lib/liba9vm-logwrap.so"

llvm-readelf -hlWd "$OUT/lib/liba9vm-logwrap.so" > "$OUT/lib/liba9vm-logwrap.so.elf.txt"
echo "logwrap: $OUT/lib/liba9vm-logwrap.so"
