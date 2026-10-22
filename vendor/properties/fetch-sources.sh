#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/container-probe/properties"
OUT="$ROOT/artifacts/container/properties/upstream"
MANIFEST="$SRC/sources.sha256"
BIONIC=https://android.googlesource.com/platform/bionic/+/290c0cb5044b643e5d6cbcb1a5b275541ca3a89e
CORE=https://android.googlesource.com/platform/system/core/+/1dea9a052b7f214c10a77d5ed6ffd3602722a817
for tool in curl base64 sha256sum; do command -v "$tool" >/dev/null; done
if [ "${1:-}" != --record ]; then test -s "$MANIFEST"; fi
temporary=
cleanup() {
    if [ -n "$temporary" ]; then rm -f -- "$temporary" "$temporary.b64"; fi
}
trap cleanup 0
while read -r repository path; do
    case "$repository" in
        bionic) base=$BIONIC ;;
        core) base=$CORE ;;
        *) exit 2 ;;
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
done < "$SRC/sources.txt"
if [ "${1:-}" = --record ]; then
    (cd "$OUT"; while read -r repository path; do sha256sum "$repository/$path"; done < "$SRC/sources.txt") > "$MANIFEST"
fi
(cd "$OUT" && sha256sum --check --status "$MANIFEST")
du -sk "$OUT"
