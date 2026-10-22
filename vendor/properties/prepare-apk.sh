#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/container-probe/properties"
OUT="$ROOT/artifacts/container/properties"
PREPARED="$ROOT/container-probe/build/prepared"
sh "$ROOT/container-probe/prepare.sh" "$@"
sh "$SRC/build-native.sh"
for binary in property-probe property-api-probe; do
    if [ "$binary" = property-probe ]; then name=liba10vm-p2-properties.so;
    else name=liba10vm-p2-property-api.so; fi
    cp "$OUT/bin/$binary" "$PREPARED/jniLibs/arm64-v8a/$name"
    sha256=$(sha256sum "$OUT/bin/$binary" | cut -d ' ' -f 1)
    needed=$(readelf -d "$OUT/bin/$binary" | \
        sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | jq -Rs 'split("\n") | map(select(length > 0))')
    jq -e 'all(. == "libc.so" or . == "libm.so" or . == "libdl.so")' \
    <<EOF >/dev/null
$needed
EOF
    jq -n --arg name "$name" --arg sha256 "$sha256" \
        --argjson bytes "$(stat -c %s "$OUT/bin/$binary")" --argjson needed "$needed" \
        '{name: $name, sha256: $sha256, bytes: $bytes, needed: $needed}' \
        >> "$PREPARED/property-files.jsonl"
done
component=$( (cd "$SRC"; sha256sum sources.sha256 platform.patch runtime.patch \
    build-library.sh build-native.sh prepare-sources.sh native/* test/*) | \
    sha256sum | cut -d ' ' -f 1)
jq -n --arg component "$component" --slurpfile files "$PREPARED/property-files.jsonl" \
    '{component: $component, bionicCommit: "290c0cb5044b643e5d6cbcb1a5b275541ca3a89e",
      coreCommit: "1dea9a052b7f214c10a77d5ed6ffd3602722a817", runtimeIntegration: false,
      propertyApiProbe: true, files: $files}' \
    > "$PREPARED/assets/properties-inputs.json"
printf 'Prepared P2 property inputs: %s\n' "$PREPARED"
