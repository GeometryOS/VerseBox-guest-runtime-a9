#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/container-probe/properties"
OUT="$ROOT/artifacts/container/properties"
D="$OUT/derived"
NDK=${ANDROID_NDK_HOME:-"$HOME/Android/Sdk/ndk/29.0.13113456"}
SYSROOT="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
unset LD_LIBRARY_PATH LD_PRELOAD
for tool in clang++ sha256sum readelf patchelf llvm-strip; do command -v "$tool" >/dev/null; done
test -d "$SYSROOT"
sh "$SRC/build-library.sh"
mkdir -p "$OUT/bin"
UNWIND=
for path in "$NDK"/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/*/lib/linux/aarch64; do
    if [ -f "$path/libunwind.a" ]; then UNWIND=$path; break; fi
done
test -n "$UNWIND"
B="$D/bionic/libc"
C="$D/core/property_service/libpropertyinfoparser"
for target in property-probe property-api-probe; do
    if [ "$target" = property-probe ]; then
        set -- "$SRC/native/PrivateProperties.cpp" "$SRC/test/PropertyProbe.cpp" \
            "$SRC/test/CrossProcess.cpp" "$OUT/lib/liba10vm-property-core.a"
    else
        set -- "$SRC/test/EarlyInit.cpp" "$SRC/test/RuntimeApiProbe.cpp" \
            "$OUT/lib/liba10vm-property-runtime.a" \
            -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc
    fi
    clang++ --target=aarch64-linux-android29 --sysroot="$SYSROOT" \
    -nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1" -nostdlib++ \
    -std=c++17 -O2 -fPIE -pie -fvisibility=hidden -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-vla-cxx-extension \
    -I"$SRC/native" -I"$B" -I"$B/async_safe/include" -I"$B/system_properties/include" \
    -I"$C/include" -I"$ROOT/container-probe/graphics/native" -L"$UNWIND" \
    -Wl,-z,max-page-size=16384 -Wl,--build-id=sha1 \
    "$SRC/native/PropertyInfo.cpp" "$SRC/test/PropertyMaps.cpp" \
    "$ROOT/container-probe/graphics/native/ProcessIdentity.cpp" \
    "$@" \
    "$SYSROOT/usr/lib/aarch64-linux-android/libc++_static.a" \
    "$SYSROOT/usr/lib/aarch64-linux-android/libc++abi.a" -ldl -lm \
    -o "$OUT/bin/$target"
    patchelf --remove-rpath "$OUT/bin/$target"
    llvm-strip --strip-unneeded "$OUT/bin/$target"
    readelf -d "$OUT/bin/$target" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'
done
sha256sum "$OUT/bin/property-probe" "$OUT/bin/property-api-probe" > "$OUT/native.sha256"
