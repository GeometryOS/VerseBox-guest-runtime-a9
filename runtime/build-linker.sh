#!/bin/sh
# Build the API 28 private ld-android.so (source-built, direct-exec capable).
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
RT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$RT/../.." && pwd)
. "$RT/compile.sh"
UP="$RT/upstream"
OUT="$RT/build"
B="$UP/bionic/libc"
mkdir -p "$OUT/obj/linker" "$OUT/obj/support" "$OUT/bin" "$OUT/generated" "$OUT/derived/bionic/linker"
test -f "$OUT/lib/liba9vm-bootstrap.a" || { echo "run build-static.sh first" >&2; exit 1; }

# Same contract as Android 10 (container-probe/runtime/dynamic/platform.patch):
# derive the private linker sources and apply the platform edits there.
python3 "$RT/patch-linker-a9.py"

builtins=$(clang --target=aarch64-linux-android28 --print-libgcc-file-name)
test -f "$builtins"

BUILD_KIND=linker
objects=
while read -r path; do
    [ -n "$path" ] || continue
    object="$OUT/obj/linker/$(printf '%s' "$path" | tr / _).o"
    if [ -f "$OUT/derived/$path" ]; then input="$OUT/derived/$path"; else input="$UP/$path"; fi
    compile "$input" "$object" -I"$MODULE/vendor/runtime/filesystem" \
        -I"$MODULE/vendor/runtime/dynamic"
    objects="$objects $object"
done < "$RT/lists/linker-sources.txt"
object="$OUT/obj/linker/LinkerFileStat.o"
compile "$MODULE/vendor/runtime/dynamic/LinkerFileStat.cpp" "$object" -I"$MODULE/vendor/runtime/filesystem"
objects="$objects $object"

support_objects=
. "$MODULE/vendor/runtime/filesystem/sources.sh"
while read -r path; do
    [ -n "$path" ] || continue
    if guest_fs_replaces "$path"; then continue; fi
    object="$OUT/obj/support/$(printf '%s' "$path" | tr / _).o"
    compile "$UP/$path" "$object"
    support_objects="$support_objects $object"
done < "$RT/lists/support-sources.txt"
rm -f "$OUT/lib/liba9vm-linker-support.a"
llvm-ar rcD "$OUT/lib/liba9vm-linker-support.a" $support_objects

ld.lld -shared -e _start -Bsymbolic --exclude-libs ALL -soname ld-android.so \
    --gc-sections -z max-page-size=4096 -z now -z relro --hash-style=gnu \
    --eh-frame-hdr --build-id=sha1 --no-undefined \
    --version-script "$UP/bionic/linker/linker.generic.map" \
    -Map "$OUT/linker.map" --error-limit=0 \
    $objects --start-group "$OUT/lib/liba9vm-linker-support.a" \
    "$OUT/lib/liba9vm-bootstrap.a" "$builtins" --end-group \
    -o "$OUT/bin/source-linker64"
llvm-readelf -hlWd "$OUT/bin/source-linker64" > "$OUT/linker-elf.txt"
llvm-nm -u "$OUT/bin/source-linker64" > "$OUT/linker-undefined.txt"
echo "linker: $OUT/bin/source-linker64"
