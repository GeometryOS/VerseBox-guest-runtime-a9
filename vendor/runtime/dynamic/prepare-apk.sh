#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SRC="$ROOT/container-probe/runtime/dynamic"
OUT="$ROOT/artifacts/container/runtime"
PREPARED="$ROOT/container-probe/build/prepared"
sh "$ROOT/container-probe/runtime/prepare-apk.sh" "$@"
sh "$SRC/build-linker.sh"
sh "$SRC/build-libc.sh"
sh "$SRC/build-probes.sh"
component=$( (cd "$SRC" && sha256sum archives.txt sources.sha256 libc-sources.sha256 \
    linker-sources.txt support-sources.txt platform.patch *.sh *.py test/*) | \
    sha256sum | cut -d ' ' -f 1)
files='[]'
while read -r path name; do
    input="$OUT/$path"
    cp "$input" "$PREPARED/jniLibs/arm64-v8a/$name"
    needed=$(llvm-readelf -d "$input" | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p' | jq -R . | jq -s .)
    files=$(printf '%s' "$files" | jq --arg name "$name" \
        --arg sha256 "$(sha256sum "$input" | cut -d ' ' -f 1)" \
        --argjson bytes "$(stat -c %s "$input")" --argjson needed "$needed" \
        '. + [{name: $name, sha256: $sha256, bytes: $bytes, needed: $needed}]')
done <<'INPUTS'
bin/source-linker64 liba10vm-source-linker.so
bin/dynamic-probe liba10vm-p2-dynamic.so
lib/libc.so liba10vm-source-libc.so
lib/libdl.so liba10vm-source-libdl.so
lib/liba10vm-tls-test.so liba10vm-tls-test.so
INPUTS
jq -n --arg component "$component" --argjson files "$files" \
    '{component: $component, tag: "android-10.0.0_r47", loaderFromSource: true,
      dynamicRuntimeIntegrated: true, completeG1: false, files: $files}' \
    > "$PREPARED/assets/dynamic-inputs.json"
printf 'Prepared source-built dynamic runtime inputs: %s\n' "$PREPARED"
