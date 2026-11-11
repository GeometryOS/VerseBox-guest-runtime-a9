#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    printf 'Usage: sh container-probe/graphics/prepare-apk.sh RAW_IMAGE G0_REPORT\n' >&2
    exit 2
fi
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/artifacts/container/graphics"
PREPARED="$ROOT/container-probe/build/prepared"
LIBS="$PREPARED/jniLibs/arm64-v8a"
unset LD_LIBRARY_PATH LD_PRELOAD
sh "$ROOT/container-probe/graphics/build-native.sh"
python3 "$ROOT/container-probe/graphics/test-native.py"
sh "$ROOT/container-probe/prepare.sh" "$1" "$2"
sh "$ROOT/container-probe/graphics/stage-apk.sh"
