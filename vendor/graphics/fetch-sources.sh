#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/artifacts/container/graphics/upstream"
QEMU=https://android.googlesource.com/platform/external/qemu/+/9e4088bf54f2a11dd7898b757c08e2c473315460
GOLDFISH=https://android.googlesource.com/device/generic/goldfish-opengl/+/d291bbd969a8cdb7bf9ac6f7d46af5b3d2b6c309
for tool in curl base64 sha256sum; do command -v "$tool" >/dev/null; done
MANIFEST="$ROOT/container-probe/graphics/sources.sha256"
test -s "$MANIFEST"
temporary=
cleanup() {
    if [ -n "$temporary" ]; then
        for file in "$temporary.b64" "$temporary"; do
            if [ -f "$file" ]; then rm -- "$file"; fi
        done
    fi
}
trap cleanup 0
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
    curl --fail --silent --show-error --max-time 30 --retry 1 --retry-delay 20 \
        "$base/$path?format=TEXT" -o "$temporary.b64"
    base64 -d "$temporary.b64" > "$temporary"
    test -s "$temporary"
    mv "$temporary" "$destination"
    rm "$temporary.b64"
    temporary=
    sleep 2
done < "$ROOT/container-probe/graphics/sources.txt"
(cd "$OUT" && sha256sum --check --status "$MANIFEST") || {
    printf 'Pinned graphics source verification failed.\n' >&2
    exit 1
}
du -sk "$OUT"
