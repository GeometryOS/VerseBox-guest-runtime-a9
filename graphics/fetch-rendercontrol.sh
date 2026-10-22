#!/bin/sh
# Fetch the renderControl codegen definitions that sources.txt omitted.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
D="$MODULE/graphics/definitions"
Q=https://android.googlesource.com/platform/external/qemu/+/9e4088bf54f2a11dd7898b757c08e2c473315460/android/android-emugl/host/libs/renderControl_dec
G=https://android.googlesource.com/device/generic/goldfish-opengl/+/d291bbd969a8cdb7bf9ac6f7d46af5b3d2b6c309/system/renderControl_enc
mkdir -p "$D"
for f in renderControl.in renderControl.attrib renderControl.types; do
    curl --fail --silent --show-error --max-time 60 --retry 3 --retry-delay 5 \
        "$Q/$f?format=TEXT" | base64 -d > "$D/$f"
    printf '%s: %s bytes\n' "$f" "$(wc -c < "$D/$f")"
    sleep 1
done
curl --fail --silent --show-error --max-time 60 --retry 3 --retry-delay 5 \
    "$G/renderControl_types.h?format=TEXT" | base64 -d > "$D/renderControl_types.h"
printf 'renderControl_types.h: %s bytes\n' "$(wc -c < "$D/renderControl_types.h")"
