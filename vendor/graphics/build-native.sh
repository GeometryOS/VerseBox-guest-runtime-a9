#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/container-probe/graphics"
OUT="$ROOT/artifacts/container/graphics"
Q="$OUT/upstream/qemu/android/android-emugl"
G="$OUT/upstream/goldfish"
D="$OUT/derived"
NDK=${ANDROID_NDK_HOME:-"$HOME/Android/Sdk/ndk/29.0.13113456"}
SYSROOT="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
unset LD_LIBRARY_PATH LD_PRELOAD
for tool in clang++ git sha256sum patchelf llvm-strip readelf; do command -v "$tool" >/dev/null; done
test -d "$SYSROOT"
sh "$SRC/fetch-sources.sh"
mkdir -p "$OUT/bin" "$OUT/generated" "$OUT/definitions" "$D/common" "$D/encoder"
clang++ -std=c++17 -O2 -I"$OUT/upstream/qemu/android/android-emu" \
    "$Q"/host/tools/emugen/*.cpp -o "$OUT/bin/emugen"
for suffix in in attrib types; do
    cp "$Q/host/libs/GLESv2_dec/gles2.$suffix" "$OUT/definitions/gl2.$suffix"
done
cp "$Q/host/libs/GLESv2_dec/gles2_types.h" "$OUT/definitions/gl2_types.h"
"$OUT/bin/emugen" -i "$OUT/definitions" -E "$OUT/generated" -D "$OUT/generated" gl2
cp "$Q/shared/OpenglCodecCommon/ChecksumCalculator.cpp" "$D/common/"
cp "$Q/shared/OpenglCodecCommon/ChecksumCalculator.h" "$D/common/"
cp "$Q/shared/OpenglCodecCommon/ChecksumCalculatorThreadInfo.h" "$D/common/"
cp "$G/shared/OpenglCodecCommon/GLSharedGroup.cpp" "$D/common/"
cp "$G/shared/OpenglCodecCommon/GLSharedGroup.h" "$D/common/"
cp "$G/system/GLESv2_enc/GL2Encoder.cpp" "$D/encoder/"
git apply --recount --unsafe-paths --directory="$D" "$SRC/platform.patch"
CODEC_ID=$( (cd "$OUT"; sha256sum definitions/gl2.in definitions/gl2.attrib \
    definitions/gl2.types definitions/gl2_types.h generated/gl2_enc.cpp generated/gl2_dec.cpp \
    derived/common/ChecksumCalculator.cpp derived/common/ChecksumCalculator.h; \
    cd "$SRC"; sha256sum platform.patch native/ProbeProtocol.h native/ProbeDecoder.cpp \
    compat/IOStream.h native/PixelReadback.cpp window/WindowProtocol.h window/TouchProtocol.h \
    window/TouchState.h window/TouchQueue.h) | sha256sum | cut -d ' ' -f 1)
printf '%s\n' "$CODEC_ID" > "$OUT/codec-id.txt"
UNWIND=
for path in "$NDK"/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/*/lib/linux/aarch64; do
    if [ -f "$path/libunwind.a" ]; then UNWIND=$path; break; fi
done
test -n "$UNWIND"
DEBUG_FLAG=-g0
if [ "${A10VM_GL_DEBUG:-0}" = 1 ]; then DEBUG_FLAG=-gline-tables-only; fi
compile() {
    clang++ --target=aarch64-linux-android28 --sysroot="$SYSROOT" \
        -nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1" -nostdlib++ \
        -std=c++17 -O2 -fPIC "$DEBUG_FLAG" -DANDROID -DPLATFORM_SDK_VERSION=29 \
        -DA10VM_NO_SNAPSHOTS -DA10VM_NO_DMA -Wno-macro-redefined \
        "-DA10VM_CODEC_ID=\"$CODEC_ID\"" \
        -include "$SRC/compat/log/log.h" \
        -I"$SRC/compat" -I"$SRC/native" -I"$SRC/window" -I"$D/common" -I"$OUT/generated" \
        -I"$OUT/definitions" -I"$G/system/GLESv2_enc" -I"$G/shared/OpenglCodecCommon" \
        -I"$Q/shared/OpenglCodecCommon" -I"$Q/guest" \
        -L"$UNWIND" \
        -Wl,-z,max-page-size=16384 -Wl,--build-id=sha1 "$@" \
        "$SYSROOT/usr/lib/aarch64-linux-android/libc++_static.a" \
        "$SYSROOT/usr/lib/aarch64-linux-android/libc++abi.a" -ldl -lm
}
COMMON="$D/common/ChecksumCalculator.cpp"
if [ "${1:-}" = --syntax-only ]; then
    compile -fsyntax-only "$D/encoder/GL2Encoder.cpp" "$OUT/generated/gl2_enc.cpp" \
        "$OUT/generated/gl2_dec.cpp" "$SRC/native/CodecPlatform.cpp" \
        "$SRC/native/SocketStream.cpp" "$COMMON"
    exit
fi
compile "$SRC/test/StreamTest.cpp" "$SRC/native/SocketStream.cpp" \
    "$SRC/native/CodecPlatform.cpp" "$COMMON" -o "$OUT/bin/stream-test"
compile "$SRC/test/TouchTest.cpp" "$SRC/native/SocketStream.cpp" \
    "$SRC/native/CodecPlatform.cpp" "$COMMON" -o "$OUT/bin/touch-test"
compile "$SRC/native/CodecClient.cpp" "$SRC/native/SocketStream.cpp" "$COMMON" \
    "$SRC/window/WindowClient.cpp" \
    "$SRC/native/ProcessIdentity.cpp" \
    "$SRC/native/PixelReadback.cpp" \
    "$D/encoder/GL2Encoder.cpp" "$G/system/GLESv2_enc/GL2EncoderUtils.cpp" \
    "$G/system/GLESv2_enc/GLESv2Validation.cpp" "$D/common/GLSharedGroup.cpp" \
    "$G/shared/OpenglCodecCommon/GLClientState.cpp" \
    "$G/shared/OpenglCodecCommon/GLESTextureUtils.cpp" \
    "$G/shared/OpenglCodecCommon/IndexRangeCache.cpp" \
    "$G/shared/OpenglCodecCommon/glUtils.cpp" "$OUT/generated/gl2_enc.cpp" \
    "$OUT/generated/gl2_client_context.cpp" -o "$OUT/bin/codec-client"
compile "$SRC/native/CodecHost.cpp" "$SRC/native/SocketStream.cpp" \
    "$SRC/native/ProcessIdentity.cpp" \
    "$SRC/native/ProbeDecoder.cpp" \
    "$SRC/native/CodecPlatform.cpp" "$COMMON" "$OUT/generated/gl2_dec.cpp" \
    "$OUT/generated/gl2_server_context.cpp" -lEGL -lGLESv2 -o "$OUT/bin/codec-host"
compile -shared "$SRC/window/WindowJni.cpp" "$SRC/window/WindowRenderer.cpp" \
    "$SRC/window/WindowEgl.cpp" "$SRC/native/SocketStream.cpp" "$SRC/native/ProbeDecoder.cpp" \
    "$SRC/native/CodecPlatform.cpp" "$COMMON" "$OUT/generated/gl2_dec.cpp" \
    "$OUT/generated/gl2_server_context.cpp" -lEGL -lGLESv2 -landroid \
    -Wl,-soname,liba10vm-p1-window.so -o "$OUT/bin/liba10vm-p1-window.so"
compile "$SRC/test/WindowHost.cpp" "$SRC/window/WindowRenderer.cpp" "$SRC/window/WindowEgl.cpp" \
    "$SRC/native/SocketStream.cpp" "$SRC/native/ProbeDecoder.cpp" "$SRC/native/CodecPlatform.cpp" \
    "$COMMON" "$OUT/generated/gl2_dec.cpp" "$OUT/generated/gl2_server_context.cpp" \
    -lEGL -lGLESv2 -landroid -o "$OUT/bin/window-host"
for name in stream-test touch-test codec-client codec-host window-host liba10vm-p1-window.so; do
    patchelf --remove-rpath "$OUT/bin/$name"
    if [ "${A10VM_GL_DEBUG:-0}" != 1 ]; then llvm-strip --strip-unneeded "$OUT/bin/$name"; fi
    readelf -d "$OUT/bin/$name" | grep -q 'Shared library: \[libc.so\]'
done
sha256sum "$OUT"/definitions/gl2.* "$OUT"/generated/gl2_* "$SRC/platform.patch" \
    > "$OUT/codec-sha256.txt"
SHARED="$ROOT/native-display/build/prepared/jniLibs/arm64-v8a"
mkdir -p "$SHARED"
cp "$OUT/bin/liba10vm-p1-window.so" "$SHARED/"
printf 'Built paired GL codec: %s/bin\n' "$OUT"
