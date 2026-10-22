#!/bin/sh
# Fetch the Android 9 (API 28) framework headers the guest graphics libraries
# compile against, plus the goldfish-opengl "system" tree at the commit the
# Android 10 desktop build already pins.
#
# Why a separate manifest: container-probe/graphics/sources.txt only lists the
# paired-codec files.  The EGL/GLES/gralloc/HWC sources and the framework
# headers (opengl, hardware, nativewindow, nativebase, ui, arect, libsync)
# lived in artifacts/ on the Android 10 build machine and were never checked
# in, so the A9 tree has to fetch its own copy.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$MODULE/graphics/framework"
TAG=refs/tags/android-9.0.0_r61
GOLDFISH_COMMIT=d291bbd969a8cdb7bf9ac6f7d46af5b3d2b6c309
mkdir -p "$OUT"

fetch_platform() {
    repository=$1
    subtree=$2
    destination="$OUT/$repository/$subtree"
    if [ -d "$destination" ] && [ -n "$(ls -A "$destination" 2>/dev/null)" ]; then
        echo "have  $repository/$subtree"
        return
    fi
    mkdir -p "$destination"
    echo "fetch $repository/$subtree ($TAG)"
    curl --fail --silent --show-error --max-time 120 --retry 3 --retry-delay 5 \
        "https://android.googlesource.com/platform/$repository/+archive/$TAG/$subtree.tar.gz" \
        -o "$destination.tar.gz.part"
    tar -xzf "$destination.tar.gz.part" -C "$destination"
    rm "$destination.tar.gz.part"
}

# framework headers the goldfish guest sources include
fetch_platform frameworks/native opengl/include
fetch_platform frameworks/native libs/nativewindow/include
fetch_platform frameworks/native libs/nativebase/include
fetch_platform frameworks/native libs/ui/include
fetch_platform frameworks/native libs/arect/include
fetch_platform hardware/libhardware include
fetch_platform system/core libsync/include

# goldfish-opengl guest implementation (egl/gralloc/hwc2/GLESv1/OpenglSystemCommon)
GOLDFISH_OUT="$MODULE/graphics/upstream/goldfish/system"
# The pinned sources.txt already staged system/GLESv2_enc; a half tree must not
# look complete, so the marker is what makes a re-run idempotent.
if [ -f "$GOLDFISH_OUT/.complete" ]; then
    echo "have  goldfish/system"
else
    rm -rf "$GOLDFISH_OUT"
    mkdir -p "$GOLDFISH_OUT"
    echo "fetch goldfish-opengl system ($GOLDFISH_COMMIT)"
    curl --fail --silent --show-error --max-time 120 --retry 3 --retry-delay 5 \
        "https://android.googlesource.com/device/generic/goldfish-opengl/+archive/$GOLDFISH_COMMIT/system.tar.gz" \
        -o "$GOLDFISH_OUT.tar.gz.part"
    tar -xzf "$GOLDFISH_OUT.tar.gz.part" -C "$GOLDFISH_OUT"
    rm "$GOLDFISH_OUT.tar.gz.part"
    touch "$GOLDFISH_OUT/.complete"
fi

echo "framework headers: $(find "$OUT" -type f | wc -l) files"
echo "goldfish system:   $(find "$GOLDFISH_OUT" -type f | wc -l) files"

# goldfish/shared: gralloc_cb.h and the codec-common headers/sources that the
# pinned sources.txt does not enumerate (it only carried the encoder subset).
GOLDFISH_SHARED="$MODULE/graphics/upstream/goldfish/shared"
if [ -f "$GOLDFISH_SHARED/.complete" ]; then
    echo "have  goldfish/shared"
else
    mkdir -p "$GOLDFISH_SHARED"
    echo "fetch goldfish-opengl shared ($GOLDFISH_COMMIT)"
    curl --fail --silent --show-error --max-time 120 --retry 3 --retry-delay 5 \
        "https://android.googlesource.com/device/generic/goldfish-opengl/+archive/$GOLDFISH_COMMIT/shared.tar.gz" \
        -o "$GOLDFISH_SHARED.tar.gz.part"
    tar -xzf "$GOLDFISH_SHARED.tar.gz.part" -C "$GOLDFISH_SHARED"
    rm "$GOLDFISH_SHARED.tar.gz.part"
    touch "$GOLDFISH_SHARED/.complete"
fi
echo "goldfish shared:   $(find "$GOLDFISH_SHARED" -type f | wc -l) files"
