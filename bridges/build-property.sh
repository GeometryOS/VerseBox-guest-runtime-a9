#!/bin/sh
# Build the API 28 property bridge:
#   liba9vm-property-client.so  (LD_PRELOAD socket client)
#   property-service            (guest-side property area server)
#   seed-properties             (one-shot initial property writer)
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
. "$(dirname -- "$0")/compile.sh"

D="$RT/build/derived"
PB="$D/bionic/libc"
PC="$D/core/property_service/libpropertyinfoparser"
PROP="$MODULE/vendor/properties"
GEN="$MODULE/vendor/native"
OBJ="$OUT/obj/property"
mkdir -p "$OBJ" "$OUT/bin" "$OUT/lib"

test -f "$RT/build/lib/libc.so" || { echo "run build-libc.sh first" >&2; exit 1; }
test -d "$PB/system_properties" || { echo "run build-static.sh first (derived)" >&2; exit 1; }

crt_so
crt_pie
builtins=$(clang --target=$TARGET --print-libgcc-file-name)

# The A9 property core, built from the patched A9 sources.
core_objects=
for path in \
    "$PB/system_properties/context_node.cpp" \
    "$PB/system_properties/contexts_serialized.cpp" \
    "$PB/system_properties/prop_area.cpp" \
    "$PB/system_properties/prop_info.cpp" \
    "$PB/system_properties/system_properties.cpp" \
    "$PC/property_info_parser.cpp" \
    "$UP/bionic/libc/async_safe/async_safe_log.cpp"; do
    object="$OBJ/core-$(basename "$path").o"
    platform_compile "$path" "$object"
    core_objects="$core_objects $object"
done

# Shared property client (pure C).
platform_compile "$GEN/PropertyClient.c" "$OBJ/property-client.o"
ld.lld -shared -soname liba9vm-property-client.so -z now -z relro -z max-page-size=4096 \
    --no-undefined "$OUT/obj/crt/crtbegin_so.o" "$OBJ/property-client.o" \
    "$LIBC" "$OUT/obj/crt/crtend_so.o" \
    -o "$OUT/lib/liba9vm-property-client.so"

# property-service: independent PIE started by the guest linker.
# Same 29/28 issue as seed-properties below: the shared source validates the
# A10 SDK level, so A9 uses its own frozen copy.
service_objects=
for path in "$PROP/native/PrivateProperties.cpp" "$PROP/native/PropertyInfo.cpp" \
            "$MODULE/native/PropertyServiceA9.cpp"; do
    object="$OBJ/$(basename "$path").o"
    platform_compile "$path" "$object"
    service_objects="$service_objects $object"
done
ld.lld -pie -e _start -dynamic-linker /system/bin/linker64 \
    -z now -z relro -z max-page-size=4096 --no-undefined --exclude-libs ALL \
    "$OUT/obj/crt/crtbegin.o" $core_objects $service_objects \
    "$LIBCXX" "$LIBC" "$LIBM" "$LIBDL" "$LINKER" "$builtins" \
    "$OUT/obj/crt/crtend.o" -o "$OUT/bin/property-service"

# seed-properties: independent PIE, writes the initial property area.
# The Android 10 shared source hardcodes ro.build.version.sdk == 29; the A9
# copy checks 28 instead.  Using the shared file here made the seed exit 1
# with an empty log, which looked like a loader failure but was not.
seed_objects=
for path in "$PROP/native/PrivateProperties.cpp" "$PROP/native/PropertyInfo.cpp" \
            "$MODULE/native/SeedPropertiesA9.cpp"; do
    object="$OBJ/seed-$(basename "$path").o"
    platform_compile "$path" "$object"
    seed_objects="$seed_objects $object"
done
ld.lld -pie -e _start -dynamic-linker /system/bin/linker64 \
    -z now -z relro -z max-page-size=4096 --no-undefined --exclude-libs ALL \
    "$OUT/obj/crt/crtbegin.o" $core_objects $seed_objects \
    "$LIBCXX" "$LIBC" "$LIBM" "$LIBDL" "$LINKER" "$builtins" \
    "$OUT/obj/crt/crtend.o" -o "$OUT/bin/seed-properties"

for f in "$OUT/lib/liba9vm-property-client.so" "$OUT/bin/property-service" "$OUT/bin/seed-properties"; do
    llvm-readelf -hlWd "$f" > "$f.elf.txt"
done
llvm-nm -D --defined-only "$OUT/lib/liba9vm-property-client.so" | grep -E '__system_property' || true
echo "property: $OUT/lib/liba9vm-property-client.so"
echo "property: $OUT/bin/property-service"
echo "property: $OUT/bin/seed-properties"
