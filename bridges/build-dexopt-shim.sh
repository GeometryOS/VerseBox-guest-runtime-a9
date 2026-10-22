#!/bin/sh
# Build liba9vm-dexopt-shim.so (see dexopt-shim.c for why it exists).
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
. "$(dirname -- "$0")/compile.sh"

OBJ="$OUT/obj/dexopt"
mkdir -p "$OBJ" "$OUT/lib"
test -f "$RT/build/lib/libc.so" || { echo "run build-libc.sh first" >&2; exit 1; }

crt_so
platform_compile "$BR/dexopt-shim.c" "$OBJ/dexopt-shim.o"

ld.lld -shared -soname liba9vm-dexopt-shim.so -z now -z relro -z max-page-size=4096 \
    --no-undefined "$OUT/obj/crt/crtbegin_so.o" "$OBJ/dexopt-shim.o" \
    "$LIBDL" "$LIBC" "$OUT/obj/crt/crtend_so.o" \
    -o "$OUT/lib/liba9vm-dexopt-shim.so"

llvm-readelf -hlWd "$OUT/lib/liba9vm-dexopt-shim.so" > "$OUT/lib/liba9vm-dexopt-shim.so.elf.txt"
echo "dexopt-shim: $OUT/lib/liba9vm-dexopt-shim.so"
