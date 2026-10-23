#!/bin/sh
# Build liba9vm-healthd-sysfs.so (see healthd-sysfs.c for why it exists).
#
# API 28's healthd reads /sys/class/power_supply directly, while Android 10
# uses the project's health HAL.  This wrapper redirects only that prefix to
# the per-instance battery-sysfs directory the host keeps up to date.
set -eu
MODULE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NDK=${ANDROID_NDK_HOME:-"$HOME/Android/Sdk/ndk/29.0.14206865"}
CLANG="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
if [ ! -x "$CLANG" ]; then
    CLANG="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
fi
test -x "$CLANG"
OUT="$MODULE/bridges/lib"
OBJ="$MODULE/bridges/obj/healthd-sysfs"
mkdir -p "$OUT" "$OBJ"
"$CLANG" --target=aarch64-linux-android28 -shared -fPIC -O2 -Wall -Wextra \
    -Wl,-soname,liba9vm-healthd-sysfs.so \
    -o "$OUT/liba9vm-healthd-sysfs.so" "$MODULE/bridges/healthd-sysfs.c" -ldl
llvm-readelf -hlWd "$OUT/liba9vm-healthd-sysfs.so" > "$OUT/liba9vm-healthd-sysfs.so.elf.txt" 2>/dev/null || true
echo "healthd-sysfs: $OUT/liba9vm-healthd-sysfs.so"
