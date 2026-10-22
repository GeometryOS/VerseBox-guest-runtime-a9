#!/bin/sh
# Fetch the same pinned graphics sources the A10 build uses, into the A9 tree.
# Reusing the A10-era goldfish commit is intentional: the HAL interfaces used
# by A9 and A10 (mapper 2.0/2.1, allocator 2.0, composer 2.1) are the same.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$MODULE/graphics/upstream"
LIST="$MODULE/vendor/graphics/sources.txt"
QEMU=https://android.googlesource.com/platform/external/qemu/+/9e4088bf54f2a11dd7898b757c08e2c473315460
GOLDFISH=https://android.googlesource.com/device/generic/goldfish-opengl/+/d291bbd969a8cdb7bf9ac6f7d46af5b3d2b6c309
mkdir -p "$OUT"
while read -r repository path; do
    case "$repository" in
        ''|'#'*) continue ;;
        qemu) base=$QEMU ;;
        goldfish) base=$GOLDFISH ;;
        *) printf 'Unknown source: %s\n' "$repository" >&2; exit 1 ;;
    esac
    destination="$OUT/$repository/$path"
    if [ -s "$destination" ]; then continue; fi
    mkdir -p "${destination%/*}"
    temporary="$destination.part"
    curl --fail --silent --show-error --max-time 60 --retry 3 --retry-delay 5 \
        "$base/$path?format=TEXT" -o "$temporary.b64"
    base64 -d "$temporary.b64" > "$temporary"
    test -s "$temporary"
    mv "$temporary" "$destination"
    rm "$temporary.b64"
    sleep 1
done < "$LIST"
echo "graphics sources: $(find "$OUT" -type f | wc -l) files"
