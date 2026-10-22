#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SRC="$ROOT/container-probe/runtime/dynamic"
OUT="$ROOT/artifacts/container/runtime"
UP="$OUT/upstream"
B="$UP/bionic/libc"
D="$OUT/derived"
for tool in clang clang++ ld.lld llvm-ar llvm-readelf llvm-nm git python3; do command -v "$tool" >/dev/null; done
unset LD_LIBRARY_PATH LD_PRELOAD
sh "$SRC/fetch-sources.sh" "${1:-}"
sh "$ROOT/container-probe/runtime/build-native.sh"
mkdir -p "$OUT/obj/linker" "$OUT/bin" "$OUT/generated" "$D/bionic/linker" "$D/build/tools/fs_config"
cp "$UP/bionic/linker/linker.cpp" "$UP/bionic/linker/linker_main.cpp" \
    "$UP/bionic/linker/dlfcn.cpp" "$UP/bionic/linker/linker_config.cpp" \
    "$UP/bionic/linker/linker_phdr.cpp" "$UP/bionic/linker/linker_utils.cpp" "$D/bionic/linker/"
cp "$UP/build/tools/fs_config/fs_config_generator.py" "$D/build/tools/fs_config/"
git apply --recount --unidiff-zero --unsafe-paths --directory="$D" "$SRC/platform.patch"
python3 "$D/build/tools/fs_config/fs_config_generator.py" aidarray \
    "$UP/system/core/libcutils/include/private/android_filesystem_config.h" \
    > "$OUT/generated/generated_android_ids.h"
builtins=$(clang --target=aarch64-linux-android29 --print-libgcc-file-name)
test -f "$builtins"
clang --version > "$OUT/linker-compiler.txt"
sha256sum "$builtins" > "$OUT/linker-builtins.sha256"
BUILD_KIND=linker
. "$SRC/compile-platform.sh"
objects=
while read -r path; do
    object="$OUT/obj/linker/$(printf '%s' "$path" | tr / _).o"
    if [ -f "$D/$path" ]; then input="$D/$path"; else input="$UP/$path"; fi
    compile "$input" "$object" -I"$ROOT/container-probe/runtime/filesystem" -I"$SRC"
    objects="$objects $object"
done < "$SRC/linker-sources.txt"
object="$OUT/obj/linker/LinkerFileStat.o"
compile "$SRC/LinkerFileStat.cpp" "$object" -I"$ROOT/container-probe/runtime/filesystem"
objects="$objects $object"
support_objects=
. "$ROOT/container-probe/runtime/filesystem/sources.sh"
while read -r path; do
    if guest_fs_replaces "$path"; then continue; fi
    object="$OUT/obj/linker/$(printf '%s' "$path" | tr / _).o"
    compile "$UP/$path" "$object"
    support_objects="$support_objects $object"
done < "$SRC/support-sources.txt"
rm -f "$OUT/lib/liba10vm-linker-support.a"
llvm-ar rcD "$OUT/lib/liba10vm-linker-support.a" $support_objects
ld.lld -shared -e _start -Bsymbolic --exclude-libs ALL -soname ld-android.so \
    --gc-sections -z max-page-size=4096 -z now -z relro --hash-style=gnu \
    --eh-frame-hdr --build-id=sha1 --no-undefined \
    --version-script "$UP/bionic/linker/linker.generic.map" \
    -Map "$OUT/linker.map" --error-limit=0 \
    $objects --start-group "$OUT/lib/liba10vm-linker-support.a" \
    "$OUT/lib/liba10vm-bootstrap.a" "$builtins" --end-group \
    -o "$OUT/bin/source-linker64"
llvm-readelf -hlWd "$OUT/bin/source-linker64" > "$OUT/linker-elf.txt"
llvm-nm -u "$OUT/bin/source-linker64" > "$OUT/linker-undefined.txt"
if [ "${1:-}" = --record ]; then
    python3 "$ROOT/container-probe/runtime/record-sources.py" "$UP" "$SRC/sources.sha256" \
        $objects $support_objects
    (cd "$UP" && sha256sum bionic/linker/linker.generic.map) >> "$SRC/sources.sha256"
    (cd "$UP" && sha256sum build/tools/fs_config/fs_config_generator.py) >> "$SRC/sources.sha256"
fi
