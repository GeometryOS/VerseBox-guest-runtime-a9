#!/bin/sh
set -eu
PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SRC="$PROJECT/container-probe/runtime/filesystem"
OUT="$PROJECT/artifacts/container/runtime"
PREPARED="$PROJECT/container-probe/build/prepared"
sh "$PROJECT/container-probe/runtime/dynamic/prepare-apk.sh" "$@"
sh "$SRC/build-probe.sh"
component=$( (cd "$SRC" && sha256sum Guest* *.sh *.py test/*) | sha256sum | cut -d ' ' -f 1)
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
bin/filesystem-probe liba10vm-p2-filesystem.so
lib/libc.so liba10vm-source-libc.so
lib/libdl.so liba10vm-source-libdl.so
INPUTS
jq -n --arg component "$component" --argjson files "$files" \
    '{component: $component, tag: "android-10.0.0_r47", rootRequired: false,
      completeG1: false, desktopVerified: false, files: $files}' \
    > "$PREPARED/assets/filesystem-inputs.json"
printf 'Prepared rootless filesystem/exec inputs: %s\n' "$PREPARED"
