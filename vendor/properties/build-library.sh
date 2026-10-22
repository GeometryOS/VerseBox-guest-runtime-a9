#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/container-probe/properties"
OUT="$ROOT/artifacts/container/properties"
D="$OUT/derived"
NDK=${ANDROID_NDK_HOME:-"$HOME/Android/Sdk/ndk/29.0.13113456"}
SYSROOT="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
unset LD_LIBRARY_PATH LD_PRELOAD
for tool in clang++ git sha256sum llvm-ar llvm-readelf llvm-nm; do command -v "$tool" >/dev/null; done
test -d "$SYSROOT"
sh "$SRC/prepare-sources.sh"
mkdir -p "$OUT/obj" "$OUT/lib"
B="$D/bionic/libc"
C="$D/core/property_service/libpropertyinfoparser"
set -- "$B/system_properties/context_node.cpp" "$B/system_properties/contexts_serialized.cpp" \
    "$B/system_properties/prop_area.cpp" "$B/system_properties/prop_info.cpp" \
    "$B/system_properties/system_properties.cpp" "$C/property_info_parser.cpp" \
    "$B/async_safe/async_safe_log.cpp" "$B/bionic/system_property_api.cpp"
objects=
for input do
    object="$OUT/obj/$(basename "$input" .cpp).o"
    clang++ --target=aarch64-linux-android29 --sysroot="$SYSROOT" \
        -nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1" \
        -std=c++17 -O2 -fPIC -fvisibility=hidden -fno-stack-protector \
        -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
        -Wall -Wextra -Werror -Wno-unused-parameter -Wno-vla-cxx-extension \
        -I"$B" -I"$B/include" -I"$B/async_safe/include" \
        -I"$B/system_properties/include" -I"$C/include" -c "$input" -o "$object"
    objects="$objects $object"
done
# API object is kept out of the standalone component, whose libc remains the control.
llvm-ar rcD "$OUT/lib/liba10vm-property-core.a" \
    "$OUT/obj/context_node.o" "$OUT/obj/contexts_serialized.o" "$OUT/obj/prop_area.o" \
    "$OUT/obj/prop_info.o" "$OUT/obj/system_properties.o" "$OUT/obj/property_info_parser.o" \
    "$OUT/obj/async_safe_log.o"
llvm-ar rcD "$OUT/lib/liba10vm-property-runtime.a" $objects
llvm-readelf -SW "$OUT/obj/system_property_api.o" > "$OUT/api-sections.txt"
if rg -q '\.(init_array|fini_array)' "$OUT/api-sections.txt"; then
    printf 'Unexpected property API static initializer/destructor\n' >&2
    exit 1
fi
llvm-nm -u "$OUT/lib/liba10vm-property-runtime.a" > "$OUT/runtime-undefined.txt"
if rg -q ' U (v?snprintf|v?fprintf|fputc|fwrite|printf|malloc|calloc|realloc)$' "$OUT/runtime-undefined.txt"; then
    printf 'Unexpected stdio/allocation dependency in property runtime slice\n' >&2
    exit 1
fi
sha256sum "$OUT/lib/"*.a > "$OUT/library.sha256"
printf 'Property runtime source archive built; no API static initializers or stdio dependencies.\n'
