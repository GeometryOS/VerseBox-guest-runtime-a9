#!/bin/sh
# Build the API 28 static Bionic archive used by ld-android.so.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
RT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$RT/../.." && pwd)
. "$RT/compile.sh"
UP="$RT/upstream"
B="$UP/bionic/libc"
OUT="$RT/build"
DERIVED="$OUT/derived"
mkdir -p "$OUT/obj/static" "$OUT/lib" "$OUT/generated"

# The project's property-area patch applies unchanged to API 28 sources.
rm -rf "$DERIVED"
mkdir -p "$DERIVED/bionic/libc" "$DERIVED/core/property_service"
cp -R "$B/system_properties" "$DERIVED/bionic/libc/"
mkdir -p "$DERIVED/bionic/libc/bionic"
cp "$B/bionic/system_property_api.cpp" "$DERIVED/bionic/libc/bionic/"
cp -R "$UP/system/core/property_service/libpropertyinfoparser" "$DERIVED/core/property_service/"
( cd "$DERIVED" && git init -q . 2>/dev/null || true )
( cd "$DERIVED" && git apply --recount --unidiff-zero --unsafe-paths --directory=. \
    "$MODULE/vendor/properties/platform.patch" \
    "$MODULE/vendor/properties/runtime.patch" )

# generated_android_ids.h is only consumed by grp_pwd.cpp in the shared libc;
# it is not part of the linker's static closure.

BUILD_KIND=static
. "$MODULE/vendor/runtime/filesystem/sources.sh"
objects=
while read -r path; do
    [ -n "$path" ] || continue
    if guest_fs_replaces "$path"; then continue; fi
    case "$path" in
        bionic/libc/arch-arm64/syscalls/prctl.S) continue ;;
    esac
    object="$OUT/obj/static/$(printf '%s' "$path" | tr / _).o"
    case "$path" in
        bionic/libc/arch-arm64/bionic/syscall.S)
            compile "$UP/$path" "$object" -Dsyscall=a10vm_kernel_syscall ;;
        *) compile "$UP/$path" "$object" ;;
    esac
    objects="$objects $object"
done < "$RT/lists/static-sources.txt"

for path in GuestPaths GuestFileCalls GuestProcMaps GuestExec GuestRealpath \
            GuestOwnership GuestIdentity GuestXattrs GuestTimers GuestSockets \
            GuestSandbox GuestSyscalls; do
    object="$OUT/obj/static/$path.o"
    if [ "$path" = GuestSyscalls ]; then
        compile "$MODULE/vendor/runtime/filesystem/$path.cpp" "$object"
    else
        compile "$MODULE/vendor/runtime/filesystem/$path.cpp" "$object" -Dsyscall=a10vm_kernel_syscall
    fi
    objects="$objects $object"
done
object="$OUT/obj/static/GuestSyscalls.S.o"
compile "$MODULE/vendor/runtime/filesystem/GuestSyscalls.S" "$object"
objects="$objects $object"

for path in system_properties/context_node.cpp system_properties/contexts_serialized.cpp \
            system_properties/prop_area.cpp system_properties/prop_info.cpp \
            system_properties/system_properties.cpp bionic/system_property_api.cpp \
            async_safe/async_safe_log.cpp; do
    object="$OUT/obj/static/property_$(basename "$path" .cpp).o"
    if [ -f "$DERIVED/bionic/libc/$path" ]; then source="$DERIVED/bionic/libc/$path";
    else source="$B/$path"; fi
    compile "$source" "$object"
    objects="$objects $object"
done
object="$OUT/obj/static/property_info_parser.o"
compile "$DERIVED/core/property_service/libpropertyinfoparser/property_info_parser.cpp" "$object"
objects="$objects $object"

rm -f "$OUT/lib/liba9vm-bootstrap.a"
llvm-ar rcD "$OUT/lib/liba9vm-bootstrap.a" $objects
echo "static archive: $OUT/lib/liba9vm-bootstrap.a"
