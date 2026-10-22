#!/bin/sh
# Build the API 28 shared libm.so from the AOSP 9 Bionic math sources.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
RT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$RT/../.." && pwd)
. "$RT/compile.sh"
UP="$RT/upstream"
B="$UP/bionic/libc"
OUT="$RT/build"
mkdir -p "$OUT/obj/libm" "$OUT/lib" "$OUT/generated"
test -f "$OUT/lib/libc.so" || { echo "run build-libc.sh first" >&2; exit 1; }

python3 "$RT/select-module-sources.py" "$UP" "$UP/bionic/libm/Android.bp" libm arm64 \
    > "$OUT/generated/libm-sources.txt"
tr -d '\r' < "$UP/bionic/libm/libm.arm64.map" > "$OUT/generated/libm.map"

builtins=$(clang --target=aarch64-linux-android28 --print-libgcc-file-name)
test -f "$builtins"
compile "$B/arch-common/bionic/crtbegin_so.c" "$OUT/obj/libm/crtbegin_so.o"
compile "$B/arch-common/bionic/crtend_so.S" "$OUT/obj/libm/crtend_so.o"

objects=
extra="-D_LIBC=1 -D__BIONIC_NO_MATH_INLINES -D_BSD_SOURCE -DFLT_EVAL_METHOD=0 \
    -I$UP/bionic/libm -I$UP/bionic/libm/upstream-freebsd/android/include \
    -I$UP/bionic/libm/upstream-freebsd/lib/msun/src \
    -I$UP/bionic/libm/upstream-freebsd/lib/msun/ld128 -I$B \
    -Wno-missing-braces -Wno-parentheses -Wno-sign-compare -Wno-uninitialized \
    -Wno-unknown-pragmas -Wno-macro-redefined -Wno-deprecated-non-prototype \
    -Wno-implicit-const-int-float-conversion"
while read -r path; do
    path=$(printf '%s' "$path" | tr -d '\r')
    [ -n "$path" ] || continue
    object="$OUT/obj/libm/$(printf '%s' "$path" | tr / _).o"
    # Absolute path: the generic -I order would otherwise resolve
    # "freebsd-compat.h" to libc's copy, which lacks digittoint.
    # Assembler sources must not pre-include C headers.
    case "$path" in
        *.S) compile "$UP/$path" "$object" -I"$UP/bionic/libm" -I"$UP/bionic/libm/arm64" ;;
        *) compile "$UP/$path" "$object" $extra -include "$UP/bionic/libm/freebsd-compat.h" ;;
    esac
    objects="$objects $object"
done < "$OUT/generated/libm-sources.txt"

ld.lld -shared -soname libm.so -z max-page-size=4096 -z now -z relro \
    --hash-style=gnu --eh-frame-hdr --build-id=sha1 --no-undefined \
    -Bsymbolic-functions --exclude-libs ALL \
    --version-script "$OUT/generated/libm.map" \
    "$OUT/obj/libm/crtbegin_so.o" $objects "$builtins" "$OUT/lib/libc.so" \
    "$OUT/obj/libm/crtend_so.o" -o "$OUT/lib/libm.so"
llvm-readelf -hlWd "$OUT/lib/libm.so" > "$OUT/libm-elf.txt"
llvm-nm -u "$OUT/lib/libm.so" > "$OUT/libm-undefined.txt"
echo "libm: $OUT/lib/libm.so"
