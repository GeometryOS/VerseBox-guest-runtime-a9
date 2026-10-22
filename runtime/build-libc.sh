#!/bin/sh
# Build the API 28 shared Bionic closure: libdl.so + libc.so.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
RT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$RT/../.." && pwd)
. "$RT/compile.sh"
UP="$RT/upstream"
B="$UP/bionic/libc"
OUT="$RT/build"
DERIVED="$OUT/derived"
mkdir -p "$OUT/obj/libc" "$OUT/obj/libdl" "$OUT/lib" "$OUT/generated"
test -f "$OUT/bin/source-linker64" || { echo "run build-linker.sh first" >&2; exit 1; }
test -f "$OUT/lib/liba9vm-bootstrap.a" || { echo "run build-static.sh first" >&2; exit 1; }
test -d "$DERIVED/bionic/libc/system_properties" || { echo "run build-static.sh first (derived)" >&2; exit 1; }

python3 "$RT/select-libc-sources.py" "$UP" > "$OUT/generated/libc-sources.txt"
ANDROID_BUILD_TOP="$UP" python3 "$B/tools/genversion-scripts.py"
tr -d '\r' < "$B/libc.arm64.map" > "$OUT/generated/libc.map"
tr -d '\r' < "$UP/bionic/libdl/libdl.arm64.map" > "$OUT/generated/libdl.map"

builtins=$(clang --target=aarch64-linux-android28 --print-libgcc-file-name)
test -f "$builtins"

BUILD_KIND=shared
dl_objects=
for path in libdl.cpp libdl_cfi.cpp; do
    object="$OUT/obj/libdl/$path.o"
    compile "$UP/bionic/libdl/$path" "$object"
    dl_objects="$dl_objects $object"
done
ld.lld -shared -soname libdl.so -z max-page-size=4096 -z now -z relro \
    --hash-style=gnu --eh-frame-hdr --build-id=sha1 --no-undefined --exclude-libs ALL \
    --version-script "$OUT/generated/libdl.map" $dl_objects "$OUT/bin/source-linker64" \
    "$builtins" -o "$OUT/lib/libdl.so"

objects=
. "$MODULE/vendor/runtime/filesystem/sources.sh"
while read -r path; do
    path=$(printf '%s' "$path" | tr -d '\r')
    [ -n "$path" ] || continue
    if guest_fs_replaces "$path"; then continue; fi
    case "$path" in
        bionic/libc/arch-arm64/syscalls/prctl.S) continue ;;
    esac
    object="$OUT/obj/libc/$(printf '%s' "$path" | tr / _).o"
    case "$path" in
        bionic/libc/system_properties/*|bionic/libc/bionic/system_property_api.cpp|bionic/libc/async_safe/async_safe_log.cpp)
            if [ -f "$DERIVED/bionic/libc/${path#bionic/libc/}" ]; then
                input="$DERIVED/bionic/libc/${path#bionic/libc/}"
            else input="$B/${path#bionic/libc/}"; fi ;;
        bionic/libc/bionic/grp_pwd.cpp) input="$UP/$path" ;;
        *) input="$UP/$path" ;;
    esac
    # LIBC_INCREMENTAL=1 reuses existing objects for iteration. Release builds
    # must run without it so a flag change cannot hide behind a stale object.
    if [ "${LIBC_INCREMENTAL:-0}" = 1 ] && [ -f "$object" ]; then
        objects="$objects $object"
        continue
    fi
    case "$path" in
        bionic/libc/bionic/grp_pwd.cpp)
            compile "$input" "$object" -I"$UP/system/core/property_service/libpropertyinfoparser/include" \
                -I"$OUT/generated" -I"$B/bionic" ;;
        bionic/libc/arch-arm64/bionic/syscall.S)
            compile "$input" "$object" -Dsyscall=a10vm_kernel_syscall ;;
        bionic/libc/bionic/bionic_systrace.cpp)
            compile "$UP/$path" "$object" ;;
        bionic/libc/upstream-freebsd/lib/libc/stdlib/realpath.c)
            compile "$input" "$object" -I"$UP/system/core/property_service/libpropertyinfoparser/include" \
                -Drealpath=a10vm_host_realpath -Dlstat=a10vm_host_lstat \
                -Dreadlink=a10vm_host_readlink -Dgetcwd=a10vm_host_getcwd ;;
        *) compile "$input" "$object" -I"$UP/system/core/property_service/libpropertyinfoparser/include" ;;
    esac
    objects="$objects $object"
done < "$OUT/generated/libc-sources.txt"

for path in GuestPaths GuestFileCalls GuestProcMaps GuestExec GuestRealpath \
            GuestOwnership GuestIdentity GuestXattrs GuestTimers GuestSockets \
            GuestSandbox GuestSyscalls; do
    object="$OUT/obj/libc/$path.o"
    if [ "$path" = GuestSyscalls ]; then
        compile "$MODULE/vendor/runtime/filesystem/$path.cpp" "$object"
    else
        compile "$MODULE/vendor/runtime/filesystem/$path.cpp" "$object" -Dsyscall=a10vm_kernel_syscall
    fi
    objects="$objects $object"
done
object="$OUT/obj/libc/GuestSyscalls.S.o"
compile "$MODULE/vendor/runtime/filesystem/GuestSyscalls.S" "$object"
objects="$objects $object"

object="$OUT/obj/libc/property_info_parser.o"
compile "$DERIVED/core/property_service/libpropertyinfoparser/property_info_parser.cpp" "$object"
objects="$objects $object"

# Windows caps the command line at ~32K; a response file keeps the full object
# list instead of silently dropping inputs.
# ld.lld is a native Windows binary: paths inside the response file must be
# Windows-resolvable. Relative paths are enough because the build runs in ROOT.
for object in $objects; do printf '%s\n' "${object#$ROOT/}"; done > "$OUT/obj/libc/objects.rsp"
ld.lld -shared -soname libc.so -z max-page-size=4096 -z now -z relro \
    --hash-style=gnu --eh-frame-hdr --build-id=sha1 --no-undefined --exclude-libs ALL \
    --version-script "$OUT/generated/libc.map" -Map "$OUT/libc.map" --error-limit=0 \
    "@$OUT/obj/libc/objects.rsp" "$OUT/lib/libdl.so" "$OUT/bin/source-linker64" "$builtins" \
    -o "$OUT/lib/libc.so"
llvm-readelf -hlWd "$OUT/lib/libc.so" > "$OUT/libc-elf.txt"
llvm-nm -u "$OUT/lib/libc.so" > "$OUT/libc-undefined.txt"
llvm-readelf -hlWd "$OUT/lib/libdl.so" > "$OUT/libdl-elf.txt"
sha256sum "$OUT/lib/libc.so" "$OUT/lib/libdl.so" > "$OUT/shared-libraries.sha256"
echo "libc: $OUT/lib/libc.so"
echo "libdl: $OUT/lib/libdl.so"
