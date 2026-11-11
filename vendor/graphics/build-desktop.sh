#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/container-probe/graphics"
OUT="$ROOT/artifacts/container/graphics/desktop"
UP="$ROOT/artifacts/container/runtime/upstream"
B="$UP/bionic/libc"
G="$ROOT/artifacts/container/graphics/upstream/goldfish"
Q="$ROOT/artifacts/container/graphics/upstream/qemu/android/android-emugl"
H="$ROOT/artifacts/container/graphics/upstream"
C="$ROOT/artifacts/container/graphics/derived/common"
GEN="$ROOT/artifacts/container/graphics/generated"
CACHE="$ROOT/container-probe/runtime/dynamic/compile-source.py"
RUNTIME="$ROOT/artifacts/container/runtime"
unset LD_LIBRARY_PATH LD_PRELOAD
mkdir -p "$OUT/generated" "$OUT/definitions" "$OUT/lib" "$OUT/bin" "$OUT/obj" "$OUT/derived/egl" "$OUT/derived/gralloc" "$OUT/derived/hwc2"
for suffix in in attrib types; do
    cp "$Q/host/libs/renderControl_dec/renderControl.$suffix" "$OUT/definitions/"
done
cp "$G/system/renderControl_enc/renderControl_types.h" "$OUT/definitions/"
"$ROOT/artifacts/container/graphics/bin/emugen" -i "$OUT/definitions" \
    -E "$OUT/generated" -D "$OUT/generated" renderControl
cp "$G/system/egl/egl.cpp" "$G/system/egl/eglDisplay.cpp" "$OUT/derived/egl/"
cp "$G/system/gralloc/gralloc.cpp" "$OUT/derived/gralloc/"
cp "$G/system/OpenglSystemCommon/FormatConversions.cpp" "$OUT/derived/"
cp "$G/system/hwc2/EmuHWC2.cpp" "$G/system/hwc2/EmuHWC2.h" \
    "$G/system/hwc2/MiniFence.cpp" "$G/system/hwc2/MiniFence.h" "$OUT/derived/hwc2/"
git apply --recount --unsafe-paths --directory="$OUT/derived" "$SRC/desktop/guest.patch"
git apply --recount --unsafe-paths --directory="$OUT/derived" "$SRC/desktop/config-handles.patch"
git apply --recount --unsafe-paths --directory="$OUT/derived" "$SRC/desktop/texture-contexts.patch"
git apply --recount --unsafe-paths --directory="$OUT/derived" "$SRC/desktop/format-conversions.patch"
git apply --recount --unsafe-paths --directory="$OUT/derived" "$SRC/desktop/yuv-rgb-rows.patch"
git apply --recount --unsafe-paths --directory="$OUT/derived" "$SRC/desktop/hwc.patch"
git apply --recount --unsafe-paths --directory="$OUT/derived" "$SRC/desktop/idle.patch"
git apply --recount --unsafe-paths --directory="$OUT/derived" "$SRC/desktop/native-image-lifetime.patch"
CODEC_ID=$( (cd "$ROOT/artifacts/container/graphics"; sha256sum generated/gl2_enc.cpp \
    generated/gl2_dec.cpp desktop/generated/renderControl_enc.cpp \
    desktop/generated/renderControl_dec.cpp; cd "$SRC/desktop"; \
    sha256sum HostConnection.cpp TextureUploadEncoder.h GuestTextureContexts.cpp \
    GuestTextureContexts.h texture-contexts.patch DesktopProtocol.h guest.patch) | sha256sum | cut -d ' ' -f 1)
printf '%s\n' "$CODEC_ID" > "$OUT/codec-id.txt"
. "$ROOT/container-probe/runtime/companion/compile.sh"
guest_compile() {
    compile "$@" -std=gnu++17 -DA10VM_NO_SNAPSHOTS -DA10VM_NO_DMA \
        -DPLATFORM_SDK_VERSION=29 -DEMULATOR_OPENGL_POST_O -DWITH_GLES2 \
        -DEGL_EGLEXT_PROTOTYPES -DGL_GLEXT_PROTOTYPES -DLOG_TAG=\"a10vm-gpu\" \
        -include "$Q/shared/OpenglCodecCommon/ErrorLog.h" \
        "-DA10VM_DESKTOP_CODEC_ID=\"$CODEC_ID\"" -Wno-deprecated-declarations \
        -I"$SRC/compat" -I"$SRC/native" -I"$SRC/desktop" -I"$C" \
        -I"$GEN" -I"$OUT/generated" -I"$OUT/definitions" \
        -I"$G/system/OpenglSystemCommon" -I"$G/system/GLESv1_enc" \
        -I"$G/system/GLESv2_enc" -I"$G/system/include" -I"$G/system/egl" \
        -I"$G/shared/OpenglCodecCommon" -I"$Q/shared/OpenglCodecCommon" \
        -I"$H/opengl/include" -I"$H/hardware/include" -I"$H/nativewindow/include" \
        -I"$H/nativebase/include" -I"$H/ui/include" \
        -I"$H/arect/include" \
        -I"$UP/system/core/libcutils/include" -I"$UP/system/core/liblog/include" \
        -I"$UP/system/core/libutils/include" -I"$UP/system/core/libsystem/include" \
        -I"$UP/system/core/include" -I"$UP/system/core/libsync/include"
}
link_guest() {
    name=$1
    shift
    ld.lld -shared -soname "$name" -z now -z relro -z max-page-size=4096 \
        --no-undefined "$RUNTIME/obj/binder/crtbegin_so.o" "$@" \
        "$RUNTIME/lib/libc++.so" "$RUNTIME/lib/libunwind.so" \
        "$RUNTIME/lib/libcutils.so" "$RUNTIME/lib/libutils.so" "$RUNTIME/lib/liblog.so" \
        "$RUNTIME/lib/libm.so" "$RUNTIME/lib/libdl.so" "$RUNTIME/lib/libc.so" \
        "$(clang --target=aarch64-linux-android29 --print-libgcc-file-name)" \
        "$RUNTIME/obj/binder/crtend_so.o" -o "$OUT/lib/$name"
}
for source in "$SRC/desktop/HostConnection.cpp" "$SRC/desktop/GuestTextureContexts.cpp" "$SRC/native/SocketStream.cpp" \
    "$SRC/desktop/YuvRgbRows.cpp" \
    "$SRC/native/CodecPlatform.cpp" "$SRC/native/PixelReadback.cpp" \
    "$C/ChecksumCalculator.cpp" "$C/GLSharedGroup.cpp" \
    "$G/system/OpenglSystemCommon/ThreadInfo.cpp" \
    "$OUT/derived/FormatConversions.cpp" \
    "$ROOT/artifacts/container/graphics/derived/encoder/GL2Encoder.cpp" \
    "$G/system/GLESv2_enc/GL2EncoderUtils.cpp" "$G/system/GLESv2_enc/GLESv2Validation.cpp" \
    "$G/shared/OpenglCodecCommon/GLClientState.cpp" \
    "$G/shared/OpenglCodecCommon/GLESTextureUtils.cpp" \
    "$G/shared/OpenglCodecCommon/IndexRangeCache.cpp" "$G/shared/OpenglCodecCommon/glUtils.cpp" \
    "$G/shared/OpenglCodecCommon/goldfish_dma.cpp" \
    "$G/system/GLESv1_enc/GLEncoder.cpp" "$G/system/GLESv1_enc/gl_enc.cpp" \
    "$G/system/GLESv1_enc/GLEncoderUtils.cpp" \
    "$G/system/GLESv1_enc/gl_client_context.cpp" \
    "$GEN/gl2_enc.cpp" "$GEN/gl2_client_context.cpp" \
    "$OUT/generated/renderControl_enc.cpp" "$OUT/generated/renderControl_client_context.cpp"; do
    guest_compile "$source" "$OUT/obj/$(basename "$source" .cpp).o"
done
link_guest libOpenglSystemCommon.so "$OUT/obj/"*.o
for api in GLESv1 GLESv2; do
    source="$G/system/$api/$(test "$api" = GLESv1 && printf gl || printf gl2).cpp"
    guest_compile "$source" "$OUT/$api.o"
    case "$api" in GLESv1) name=libGLESv1_CM_emulation.so ;; GLESv2) name=libGLESv2_emulation.so ;; esac
    link_guest "$name" "$OUT/$api.o" "$OUT/lib/libOpenglSystemCommon.so"
done
for source in "$OUT/derived/egl/egl.cpp" "$OUT/derived/egl/eglDisplay.cpp" \
    "$G/system/egl/ClientAPIExts.cpp"; do
    guest_compile "$source" "$OUT/$(basename "$source" .cpp).o"
done
link_guest libEGL_emulation.so "$OUT/egl.o" "$OUT/eglDisplay.o" "$OUT/ClientAPIExts.o" \
    "$OUT/lib/libOpenglSystemCommon.so" "$ROOT/artifacts/container/framework/lib/libnativewindow.so"
guest_compile "$OUT/derived/gralloc/gralloc.cpp" "$OUT/gralloc.o"
link_guest gralloc.ranchu.so "$OUT/gralloc.o" "$OUT/lib/libOpenglSystemCommon.so"
for source in "$OUT/derived/hwc2/EmuHWC2.cpp" "$OUT/derived/hwc2/MiniFence.cpp"; do
    guest_compile "$source" "$OUT/$(basename "$source" .cpp).o" -fno-rtti -I"$H/libsync/include" -I"$G/system/hwc2"
done
link_guest hwcomposer.ranchu.so "$OUT/EmuHWC2.o" "$OUT/MiniFence.o" "$OUT/lib/libOpenglSystemCommon.so" \
    "$ROOT/artifacts/container/framework/lib/libhardware.so" "$ROOT/artifacts/container/framework/lib/libsync.so"
guest_compile "$SRC/desktop/GuestHardware.cpp" "$OUT/GuestHardware.o"
link_guest liba10vm-hal-modules.so "$OUT/GuestHardware.o"
guest_compile "$SRC/desktop/HalCheck.cpp" "$OUT/HalCheck.o"
guest_compile "$SRC/desktop/HwcIdleCheck.cpp" "$OUT/HwcIdleCheck.o"
ld.lld -pie -e _start -dynamic-linker /system/bin/linker64 \
    -z now -z relro -z max-page-size=4096 --no-undefined \
    "$RUNTIME/obj/binder-probe/crtbegin.o" "$OUT/HalCheck.o" "$OUT/HwcIdleCheck.o" \
    "$ROOT/artifacts/container/framework/lib/libhardware.so" "$RUNTIME/lib/libc.so" \
    "$(clang --target=aarch64-linux-android29 --print-libgcc-file-name)" \
    "$RUNTIME/obj/binder-probe/crtend.o" -o "$OUT/bin/desktop-hal-check"
guest_compile "$SRC/desktop/FormatConversionsCheck.cpp" "$OUT/FormatConversionsCheck.o"
ld.lld -pie -e _start -dynamic-linker /system/bin/linker64 \
    -z now -z relro -z max-page-size=4096 --no-undefined \
    "$RUNTIME/obj/binder-probe/crtbegin.o" "$OUT/FormatConversionsCheck.o" \
    "$OUT/lib/libOpenglSystemCommon.so" "$RUNTIME/lib/libc++.so" "$RUNTIME/lib/libunwind.so" "$RUNTIME/lib/libc.so" \
    "$(clang --target=aarch64-linux-android29 --print-libgcc-file-name)" \
    "$RUNTIME/obj/binder-probe/crtend.o" -o "$OUT/bin/format-conversions-check"
guest_compile "$SRC/desktop/EglCheck.cpp" "$OUT/EglCheck.o"
guest_compile "$SRC/desktop/LargeTextureCheck.cpp" "$OUT/LargeTextureCheck.o"
guest_compile "$SRC/desktop/NativeBufferImageCheck.cpp" "$OUT/NativeBufferImageCheck.o"
guest_compile "$SRC/desktop/TextureMailboxCheck.cpp" "$OUT/TextureMailboxCheck.o"
guest_compile "$SRC/desktop/DanmakuTextureCheck.cpp" "$OUT/DanmakuTextureCheck.o"
guest_compile "$SRC/desktop/SharedGlyphCheck.cpp" "$OUT/SharedGlyphCheck.o"
mkdir -p "$OUT/bin"
ld.lld -pie -e _start -dynamic-linker /system/bin/linker64 \
    -z now -z relro -z max-page-size=4096 --no-undefined \
    "$RUNTIME/obj/binder-probe/crtbegin.o" "$OUT/EglCheck.o" "$OUT/LargeTextureCheck.o" "$OUT/NativeBufferImageCheck.o" "$OUT/TextureMailboxCheck.o" "$OUT/DanmakuTextureCheck.o" "$OUT/SharedGlyphCheck.o" \
    "$ROOT/artifacts/container/framework/lib/libhardware.so" \
    "$ROOT/artifacts/container/framework/lib/libEGL.so" \
    "$ROOT/artifacts/container/framework/lib/libGLESv2.so" \
    "$RUNTIME/lib/libc.so" "$RUNTIME/lib/libdl.so" "$RUNTIME/lib/libcutils.so" \
    "$(clang --target=aarch64-linux-android29 --print-libgcc-file-name)" \
    "$RUNTIME/obj/binder-probe/crtend.o" -o "$OUT/bin/desktop-egl-check"
printf 'Built desktop guest EGL/GLES: %s\n' "$OUT/lib"
python3 "$SRC/desktop/generate-adapters.py" "$GEN" "$OUT/generated/gl-adapters.inc"
sh "$SRC/build-desktop-host.sh"
