#!/bin/sh
# Build the Android 9 (API 28) guest graphics compatibility layer.
#
# This mirrors container-probe/graphics/build-desktop.sh, which produced the
# Android 10 libraries, but:
#   * every object targets aarch64-linux-android28;
#   * the link closure is the source-built API 28 libc/libdl/libm plus the
#     Redroid 9 libc++ (self-contained) and the ROM's libnativewindow /
#     libhardware / libsync / libcutils / libutils / liblog;
#   * framework headers come from guest-runtime-a9/graphics/framework (AOSP 9 r61).
#
# The guest side of the private GPU transport (HostConnection / SocketStream /
# codec ID handshake) is the same project source the Android 10 build uses, so
# the host endpoint does not change.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
BR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ROOT=$(CDPATH= cd -- "$BR/../.." && pwd)
. "$BR/bridges/compile.sh"          # NDK, crt_so, target=android28
set +u
RT="$MODULE/runtime"
UP="$RT/upstream"
B="$UP/bionic/libc"
ROM="$MODULE/rootfs/system/lib64"
SRC="$MODULE/vendor/graphics"
GF="$MODULE/graphics/upstream/goldfish"
QF="$MODULE/graphics/upstream/qemu/android/android-emugl"
FW="$MODULE/graphics/framework"
GEN="$MODULE/graphics/generated"
DEF="$MODULE/graphics/definitions"
OUT="$MODULE/bridges/graphics"
D="$OUT/derived"
OBJ="$OUT/obj"
mkdir -p "$OUT/lib" "$OUT/bin" "$OBJ" "$D/common" "$D/encoder" "$D/egl" "$D/gralloc" "$D/hwc2"

unset LD_LIBRARY_PATH LD_PRELOAD || true

# The 64-byte id is a guard over the emugen generation inputs, not the wire
# format: both sides are generated from the same pinned qemu/goldfish
# definitions.  It must equal the host desktop-gpu-host id, so the default is
# this repository's bridges/graphics/codec-id.txt.  When this tree is built
# inside the VM workspace a prebuilt Android 10 peer can be pointed at with
# A10VM_HOST_RENDERER / A10VM_A10_GUEST_LIBS, but neither is required.
codec_id_from() {
    grep -aoE '[0-9a-f]{64}' "$1" 2>/dev/null | head -n 1
}
CODEC_ID=""
if [ -f "$MODULE/bridges/graphics/codec-id.txt" ]; then
    CODEC_ID=$(tr -d '[:space:]' < "$MODULE/bridges/graphics/codec-id.txt")
fi
for candidate in "${A10VM_HOST_RENDERER:-}" "${A10VM_A10_GUEST_LIBS:-}"; do
    [ -n "$CODEC_ID" ] && break
    [ -n "$candidate" ] && [ -f "$candidate" ] && CODEC_ID=$(codec_id_from "$candidate")
done
case "$CODEC_ID" in
    ????????????????????????????????????????????????????????????????) ;;
    *) echo "Cannot determine the desktop GPU codec id" >&2; exit 1 ;;
esac
printf '%s\n' "$CODEC_ID" > "$OUT/codec-id.txt"

# ---------------------------------------------------------------- derived tree
cp "$GF/system/egl/egl.cpp" "$GF/system/egl/eglDisplay.cpp" "$D/egl/"
cp "$GF/system/gralloc/gralloc.cpp" "$D/gralloc/"
cp "$GF/system/OpenglSystemCommon/FormatConversions.cpp" "$D/"
cp "$GF/system/hwc2/EmuHWC2.cpp" "$GF/system/hwc2/EmuHWC2.h" \
   "$GF/system/hwc2/MiniFence.cpp" "$GF/system/hwc2/MiniFence.h" "$D/hwc2/"
cp "$QF/shared/OpenglCodecCommon/ChecksumCalculator.cpp" \
   "$QF/shared/OpenglCodecCommon/ChecksumCalculator.h" \
   "$QF/shared/OpenglCodecCommon/ChecksumCalculatorThreadInfo.h" "$D/common/"
cp "$GF/shared/OpenglCodecCommon/GLSharedGroup.cpp" \
   "$GF/shared/OpenglCodecCommon/GLSharedGroup.h" "$D/common/"
cp "$GF/system/GLESv2_enc/GL2Encoder.cpp" "$D/encoder/"

apply_patch() {
    patch=$1
    # The desktop patches carry hand-trimmed hunk headers, so GNU patch rejects
    # them; git apply --recount is what the Android 10 build used.
    if (cd "$MODULE" && git apply --recount --unsafe-paths --directory="$D" "$patch"); then
        echo "patched $(basename "$patch")"
    elif (cd "$MODULE" && git apply --recount --unsafe-paths -R --check --directory="$D" "$patch" >/dev/null 2>&1); then
        echo "already $(basename "$patch")"
    else
        echo "patch failed: $patch" >&2
        exit 1
    fi
}
apply_patch "$SRC/platform.patch"
apply_patch "$SRC/desktop/guest.patch"
apply_patch "$SRC/desktop/config-handles.patch"
apply_patch "$SRC/desktop/texture-contexts.patch"
apply_patch "$SRC/desktop/format-conversions.patch"
apply_patch "$SRC/desktop/yuv-rgb-rows.patch"
apply_patch "$SRC/desktop/hwc.patch"
apply_patch "$SRC/desktop/idle.patch"
apply_patch "$SRC/desktop/native-image-lifetime.patch"
# A9-only corrections where the pinned goldfish code assumes the Q HAL headers.
apply_patch "$MODULE/graphics/patches/gralloc-api28.patch"

# ---------------------------------------------------------------- compile flags
gfx_cflags() {
    printf '%s\n' \
        --target=$TARGET --sysroot="$SYSROOT" \
        -fPIC -O2 -fno-stack-protector -fno-emulated-tls -mno-outline-atomics \
        -ffunction-sections -fdata-sections -U_FORTIFY_SOURCE \
        -DANDROID -DPLATFORM_SDK_VERSION=28 \
        -DA10VM_NO_SNAPSHOTS -DA10VM_NO_DMA -DEMULATOR_OPENGL_POST_O \
        -DWITH_GLES2 -DEGL_EGLEXT_PROTOTYPES -DGL_GLEXT_PROTOTYPES \
        -DLOG_TAG=\"a9vm-gpu\" \
        "-DA10VM_DESKTOP_CODEC_ID=\"$CODEC_ID\"" \
        -Wno-deprecated-declarations -Wno-macro-redefined \
        -include "$QF/shared/OpenglCodecCommon/ErrorLog.h" \
        -I"$SRC/compat" -I"$SRC/native" -I"$SRC/desktop" -I"$D" -I"$D/common" \
        -I"$GEN" -I"$DEF" \
        -I"$GF/system/OpenglSystemCommon" -I"$GF/system/GLESv1_enc" \
        -I"$GF/system/GLESv2_enc" -I"$GF/system/include" -I"$GF/system/egl" \
        -I"$GF/system/hwc2" -I"$GF/shared/OpenglCodecCommon" \
        -I"$QF/shared/OpenglCodecCommon" \
        -I"$FW/frameworks/native/opengl/include" \
        -I"$FW/frameworks/native/libs/nativewindow/include" \
        -I"$FW/frameworks/native/libs/nativebase/include" \
        -I"$FW/frameworks/native/libs/ui/include" \
        -I"$FW/frameworks/native/libs/arect/include" \
        -I"$FW/hardware/libhardware/include" \
        -I"$FW/system/core/libsync/include" \
        -I"$UP/system/core/libcutils/include" -I"$UP/system/core/liblog/include" \
        -I"$UP/system/core/libutils/include" -I"$UP/system/core/libsystem/include" \
        -I"$UP/system/core/include" \
        -I"$B" -I"$B/private" -I"$B/async_safe/include"
}

gfx_compile() {
    input=$1
    object=$2
    shift 2
    mkdir -p "$(dirname "$object")"
    # bionic and libc++ must both come from -isystem, in this order: libc++ ships
    # its own <math.h> shim, and a plain -I bionic/include would shadow it.
    # RTTI_FLAG is -frtti by default; hwcomposer is built with -fno-rtti because
    # the ROM's libutils.so hides typeinfo for android::Thread (A10's own
    # libutils was built -fno-rtti, so the vtable has a null typeinfo slot).
    clang++ $(gfx_cflags) -nostdinc \
        -isystem "$UP/external/libcxx/include" \
        -isystem "$(clang -print-resource-dir)/include" \
        -isystem "$B/include" \
        -isystem "$B/kernel/uapi/asm-arm64" \
        -isystem "$B/kernel/uapi" \
        -isystem "$B/kernel/android/uapi" \
        "$@" -fexceptions ${RTTI_FLAG:--frtti} -std=gnu++17 -c "$input" -o "$object"
}

# ---------------------------------------------------------------- link closure
A9_LIBC="$RT/build/lib/libc.so"
A9_LIBM="$RT/build/lib/libm.so"
A9_LIBDL="$RT/build/lib/libdl.so"
A9_LIBCXX="$ROM/libc++.so"
ROM_LIBS="$ROM/libcutils.so $ROM/libutils.so $ROM/liblog.so"
crt_so
builtins=$(clang --target=$TARGET --print-libgcc-file-name)

link_guest() {
    name=$1
    shift
    ld.lld -shared -soname "$name" -z now -z relro -z max-page-size=4096 \
        --hash-style=gnu --eh-frame-hdr --build-id=sha1 --no-undefined \
        "$OUT/obj/crt/crtbegin_so.o" "$@" \
        "$A9_LIBCXX" $ROM_LIBS \
        "$A9_LIBM" "$A9_LIBDL" "$A9_LIBC" "$builtins" \
        "$OUT/obj/crt/crtend_so.o" -o "$OUT/lib/$name"
}

# ---------------------------------------------------------------- libOpenglSystemCommon
objects=
for source in \
    "$SRC/desktop/HostConnection.cpp" \
    "$SRC/desktop/GuestTextureContexts.cpp" \
    "$SRC/native/SocketStream.cpp" \
    "$SRC/desktop/YuvRgbRows.cpp" \
    "$SRC/native/CodecPlatform.cpp" \
    "$SRC/native/PixelReadback.cpp" \
    "$D/common/ChecksumCalculator.cpp" \
    "$D/common/GLSharedGroup.cpp" \
    "$GF/system/OpenglSystemCommon/ThreadInfo.cpp" \
    "$D/FormatConversions.cpp" \
    "$D/encoder/GL2Encoder.cpp" \
    "$GF/system/GLESv2_enc/GL2EncoderUtils.cpp" \
    "$GF/system/GLESv2_enc/GLESv2Validation.cpp" \
    "$GF/shared/OpenglCodecCommon/GLClientState.cpp" \
    "$GF/shared/OpenglCodecCommon/GLESTextureUtils.cpp" \
    "$GF/shared/OpenglCodecCommon/IndexRangeCache.cpp" \
    "$GF/shared/OpenglCodecCommon/glUtils.cpp" \
    "$GF/shared/OpenglCodecCommon/goldfish_dma.cpp" \
    "$GF/system/GLESv1_enc/GLEncoder.cpp" \
    "$GF/system/GLESv1_enc/gl_enc.cpp" \
    "$GF/system/GLESv1_enc/GLEncoderUtils.cpp" \
    "$GF/system/GLESv1_enc/gl_client_context.cpp" \
    "$GEN/gl2_enc.cpp" \
    "$GEN/gl2_client_context.cpp" \
    "$GEN/renderControl_enc.cpp" \
    "$GEN/renderControl_client_context.cpp" ; do
    object="$OBJ/$(basename "$source" .cpp).o"
    gfx_compile "$source" "$object"
    objects="$objects $object"
done
link_guest libOpenglSystemCommon.so $objects

# ---------------------------------------------------------------- GLES + EGL + gralloc + HWC
for api in GLESv1 GLESv2; do
    case "$api" in
        GLESv1) source="$GF/system/GLESv1/gl.cpp"; name=libGLESv1_CM_emulation.so ;;
        GLESv2) source="$GF/system/GLESv2/gl2.cpp"; name=libGLESv2_emulation.so ;;
    esac
    gfx_compile "$source" "$OBJ/$api.o"
    link_guest "$name" "$OBJ/$api.o" "$OUT/lib/libOpenglSystemCommon.so"
done

for source in "$D/egl/egl.cpp" "$D/egl/eglDisplay.cpp" "$GF/system/egl/ClientAPIExts.cpp"; do
    gfx_compile "$source" "$OBJ/$(basename "$source" .cpp).o"
done
link_guest libEGL_emulation.so "$OBJ/egl.o" "$OBJ/eglDisplay.o" "$OBJ/ClientAPIExts.o" \
    "$OUT/lib/libOpenglSystemCommon.so" "$ROM/libnativewindow.so"

gfx_compile "$D/gralloc/gralloc.cpp" "$OBJ/gralloc.o"
link_guest gralloc.ranchu.so "$OBJ/gralloc.o" "$OUT/lib/libOpenglSystemCommon.so" \
    "$ROM/libnativewindow.so" "$ROM/libhardware.so"

RTTI_FLAG=-fno-rtti
for source in "$D/hwc2/EmuHWC2.cpp" "$D/hwc2/MiniFence.cpp"; do
    gfx_compile "$source" "$OBJ/$(basename "$source" .cpp).o"
done
RTTI_FLAG=-frtti
link_guest hwcomposer.ranchu.so "$OBJ/EmuHWC2.o" "$OBJ/MiniFence.o" \
    "$OUT/lib/libOpenglSystemCommon.so" "$ROM/libhardware.so" "$ROM/libsync.so"

# HAL module name indirection (gralloc / hwcomposer / camera / audio names).
gfx_compile "$SRC/desktop/GuestHardware.cpp" "$OBJ/GuestHardware.o"
link_guest liba9vm-hal-modules.so "$OBJ/GuestHardware.o" "$A9_LIBDL"

for f in "$OUT"/lib/*.so; do
    llvm-readelf -hlWd "$f" > "$f.elf.txt"
done
echo "graphics: $(ls "$OUT"/lib/*.so | tr '\n' ' ')"
echo "codec-id: $CODEC_ID"
