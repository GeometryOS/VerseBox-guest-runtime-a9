#!/bin/sh
# Build the API 28 guest log bridge: liba9vm-guest-log.so.
# Pure C, links only the source-built API 28 libc.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
RT="$MODULE/runtime"
UP="$RT/upstream"
OUT="$MODULE/bridges"
NDK=${ANDROID_NDK_HOME:-"C:/Users/22437/AppData/Local/Android/Sdk/ndk/29.0.14206865"}
if command -v cygpath >/dev/null 2>&1; then NDK=$(cygpath -u "$NDK"); fi
export PATH="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD || true
SYSROOT="$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"
CRT="$RT/build/obj/libm"
mkdir -p "$OUT/lib" "$OUT/obj"

test -f "$RT/build/lib/libc.so" || { echo "run build-libc.sh first" >&2; exit 1; }
test -f "$CRT/crtbegin_so.o" || { echo "run build-libm.sh first (crt objects)" >&2; exit 1; }

clang --target=aarch64-linux-android28 --sysroot="$SYSROOT" \
    -fPIC -O2 -Wall -Wextra -Werror \
    -I"$UP/bionic/libc" \
    -c "$MODULE/vendor/native/GuestLog.c" -o "$OUT/obj/guest-log.o"

ld.lld -shared -soname liba9vm-guest-log.so -z now -z relro -z max-page-size=4096 \
    --no-undefined \
    "$CRT/crtbegin_so.o" "$OUT/obj/guest-log.o" \
    "$RT/build/lib/libc.so" "$CRT/crtend_so.o" \
    -o "$OUT/lib/liba9vm-guest-log.so"

llvm-readelf -hld "$OUT/lib/liba9vm-guest-log.so" > "$OUT/liba9vm-guest-log.elf.txt"
llvm-nm -D --defined-only "$OUT/lib/liba9vm-guest-log.so" | grep -E '__android_log_' || true
echo "guest-log: $OUT/lib/liba9vm-guest-log.so"
