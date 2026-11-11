#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/artifacts/container/graphics"
PREPARED="$ROOT/container-probe/build/prepared"
LIBS="$PREPARED/jniLibs/arm64-v8a"
test -f "$PREPARED/assets/inputs.json"
for side in client host window; do
    file="$LIBS/liba10vm-p1-$side.so"
    if [ "$side" = window ]; then
        rm -f "$file"
        file="$ROOT/native-display/build/prepared/jniLibs/arm64-v8a/liba10vm-p1-window.so"
    else cp "$OUT/bin/codec-$side" "$file"; fi
    case "$side" in
        client) allowed='^(libc|libm|libdl)\.so$' ;;
        host) allowed='^(libc|libm|libdl|libEGL|libGLESv2)\.so$' ;;
        window) allowed='^(libc|libm|libdl|libEGL|libGLESv2|libandroid)\.so$' ;;
    esac
    needed=$(readelf -d "$file" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | \
        jq -Rs 'split("\n") | map(select(length > 0))')
    printf '%s\n' "$needed" | jq -e --arg allowed "$allowed" \
        'length > 0 and all(.[]; test($allowed))' >/dev/null
    jq -n --arg name "$(basename "$file")" --arg sha256 "$(sha256sum "$file" | cut -d ' ' -f 1)" \
        --argjson bytes "$(stat -c %s "$file")" --argjson needed "$needed" \
        '{name: $name, sha256: $sha256, bytes: $bytes, needed: $needed}' > "$OUT/$side-input.json"
done
jq -n --arg codec "$(tr -d '\n' < "$OUT/codec-id.txt")" \
    --slurpfile client "$OUT/client-input.json" --slurpfile host "$OUT/host-input.json" \
    --slurpfile window "$OUT/window-input.json" \
    '{codec: $codec, files: [$client[0], $host[0], $window[0]], surfaceImplemented: true,
      inputProtocol: "touch-v2", targetHz: 60, desktopImplemented: false}' \
    > "$PREPARED/assets/graphics-inputs.json"
printf 'Prepared paired graphics APK inputs: %s\n' "$PREPARED"
