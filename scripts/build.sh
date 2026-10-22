#!/bin/sh
# One-shot build: image -> API 28 runtime -> bridges -> prepared.
#
# Every stage is skipped when its product already exists.  To force a rebuild,
# delete that stage's output (rootfs/, runtime/build/, bridges/lib/, prepared/).
# The per-stage order and acceptance criteria live in BUILD.md.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
cd "$MODULE"

step() { printf '\n== %s\n' "$1"; }
exists() { [ -e "$1" ]; }

if ! ls downloads/*.tar.gz >/dev/null 2>&1; then
    step "fetch Redroid 9 image"
    python scripts/fetch-redroid9.py
fi

if ! exists rootfs/system/bin/app_process64; then
    step "extract rootfs"
    python scripts/extract-rootfs.py
fi

if ! exists runtime/build/lib/libc.so; then
    step "runtime sources (AOSP android-9.0.0_r61)"
    sh runtime/fetch-a9-sources.sh
    step "static bionic"
    sh runtime/build-static.sh
    step "linker"
    sh runtime/build-linker.sh
    step "libc / libdl"
    sh runtime/build-libc.sh
    step "libm"
    sh runtime/build-libm.sh
fi

if ! exists bridges/lib/liba9vm-property-client.so; then
    step "property bridge"
    sh bridges/build-property.sh
    step "binder bridge"
    sh bridges/build-binder.sh
    step "device bridge"
    sh bridges/build-devices.sh
    step "guest log"
    sh bridges/build-guest-log.sh
fi

if ! exists bridges/graphics/lib/libEGL_emulation.so; then
    step "graphics compatibility layer"
    sh bridges/build-graphics-a9.sh
fi

step "prepared (assets + jniLibs + framework-check.dex)"
python scripts/prepare-a9.py

printf '\nbuild finished: %s/prepared\n' "$MODULE"
