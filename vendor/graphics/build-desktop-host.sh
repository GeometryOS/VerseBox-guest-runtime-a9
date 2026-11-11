#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/container-probe/graphics"
OUT="$ROOT/artifacts/container/graphics/desktop"
G="$ROOT/artifacts/container/graphics/upstream/goldfish"
Q="$ROOT/artifacts/container/graphics/upstream/qemu/android/android-emugl"
C="$ROOT/artifacts/container/graphics/derived/common"
GEN="$ROOT/artifacts/container/graphics/generated"
CODEC_ID=$(tr -d '\n' < "$OUT/codec-id.txt")
NDK=${ANDROID_NDK_HOME:-"$HOME/Android/Sdk/ndk/29.0.13113456"}
SYSROOT="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
unset LD_LIBRARY_PATH LD_PRELOAD
UNWIND=
for path in "$NDK"/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/*/lib/linux/aarch64; do
    if [ -f "$path/libunwind.a" ]; then UNWIND=$path; break; fi
done
test -n "$UNWIND"
mkdir -p "$OUT/bin"
host_compile() {
    clang++ --target=aarch64-linux-android28 --sysroot="$SYSROOT" \
        -nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1" -nostdlib++ \
        -std=c++17 -O2 -fPIC -gline-tables-only -DANDROID -DA10VM_NO_SNAPSHOTS \
        "-DA10VM_DESKTOP_CODEC_ID=\"$CODEC_ID\"" -include "$SRC/compat/log/log.h" \
        -I"$SRC/compat" -I"$SRC/native" -I"$SRC/desktop" -I"$SRC/window" -I"$C" \
        -I"$GEN" -I"$OUT/generated" -I"$OUT/definitions" \
        -I"$ROOT/artifacts/container/graphics/definitions" -I"$G/shared/OpenglCodecCommon" \
        -I"$Q/shared/OpenglCodecCommon" -I"$Q/guest" -L"$UNWIND" \
        "$@" "$SRC/desktop/GpuBackend.cpp" "$SRC/desktop/GuestGpuSync.cpp" \
        "$SRC/desktop/SharedTextureSync.cpp" "$SRC/desktop/FramebufferCopy.cpp" \
        "$SRC/desktop/ColorPixels.cpp" "$SRC/desktop/DesktopPresenter.cpp" "$SRC/desktop/DesktopDecoder.cpp" \
        "$SRC/native/SocketStream.cpp" "$SRC/native/CodecPlatform.cpp" "$C/ChecksumCalculator.cpp" \
        "$GEN/gl2_dec.cpp" "$GEN/gl2_server_context.cpp" \
        "$OUT/generated/renderControl_dec.cpp" "$OUT/generated/renderControl_server_context.cpp" \
        "$SYSROOT/usr/lib/aarch64-linux-android/libc++_static.a" \
        "$SYSROOT/usr/lib/aarch64-linux-android/libc++abi.a" -lEGL -lGLESv3 -landroid -llog -ldl -lm
}
host_compile "$SRC/desktop/DesktopHost.cpp" "$SRC/desktop/DesktopGpuServer.cpp" -o "$OUT/bin/desktop-gpu-host"
patchelf --remove-rpath "$OUT/bin/desktop-gpu-host"
host_compile "$SRC/desktop/GpuServerCheck.cpp" "$SRC/desktop/DesktopGpuServer.cpp" -o "$OUT/bin/desktop-server-check"
patchelf --remove-rpath "$OUT/bin/desktop-server-check"
host_compile "$SRC/desktop/DesktopSyncCheck.cpp" -o "$OUT/bin/desktop-sync-check"
patchelf --remove-rpath "$OUT/bin/desktop-sync-check"
host_compile -shared -DA10VM_DESKTOP_WINDOW "$SRC/window/WindowJni.cpp" \
    "$SRC/desktop/DesktopGpuServer.cpp" "$SRC/desktop/DesktopInputChannel.cpp" \
    "$SRC/desktop/DesktopRenderer.cpp" -Wl,-soname,liba10vm-desktop-window.so \
    -o "$OUT/bin/liba10vm-desktop-window.so"
patchelf --remove-rpath "$OUT/bin/liba10vm-desktop-window.so"
mkdir -p "$ROOT/native-display/build/prepared/jniLibs/arm64-v8a"
cp "$OUT/bin/liba10vm-desktop-window.so" "$ROOT/native-display/build/prepared/jniLibs/arm64-v8a/"
clang++ --target=aarch64-linux-android28 --sysroot="$SYSROOT" \
    -nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1" -nostdlib++ -std=c++17 -O2 \
    -L"$UNWIND" "$SRC/desktop/PresentationCheck.cpp" "$SRC/desktop/FramebufferCopyCheck.cpp" \
    "$SRC/desktop/ContextGroupsCheck.cpp" "$SRC/desktop/GpuBackend.cpp" "$SRC/desktop/GuestGpuSync.cpp" \
    "$SRC/desktop/SharedTextureSync.cpp" "$SRC/desktop/FramebufferCopy.cpp" "$SRC/desktop/ColorPixels.cpp" \
    "$SRC/desktop/DesktopPresenter.cpp" "$SYSROOT/usr/lib/aarch64-linux-android/libc++_static.a" \
    "$SYSROOT/usr/lib/aarch64-linux-android/libc++abi.a" -lEGL -lGLESv3 -landroid -llog -ldl -lm \
    -o "$OUT/bin/desktop-presentation-check"
patchelf --remove-rpath "$OUT/bin/desktop-presentation-check"
