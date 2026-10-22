#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SRC="$ROOT/container-probe/runtime/dynamic"
OUT="$ROOT/artifacts/container/runtime"
for tool in curl tar sha256sum; do command -v "$tool" >/dev/null; done
mkdir -p "$OUT/downloads" "$OUT/upstream"
while read -r repository commit subtree; do
    name=$(printf '%s' "$repository-$commit-$subtree" | tr / _)
    archive="$OUT/downloads/$name.tar.gz"
    destination="$OUT/upstream/$repository/$subtree"
    if [ ! -d "$destination" ]; then
        if [ ! -s "$archive" ]; then
            case "$subtree" in
                .) url="https://android.googlesource.com/platform/$repository/+archive/$commit.tar.gz" ;;
                *) url="https://android.googlesource.com/platform/$repository/+archive/$commit/$subtree.tar.gz" ;;
            esac
            curl --fail --silent --show-error --max-time 60 --retry 1 --retry-delay 20 \
                "$url" -o "$archive.part"
            mv "$archive.part" "$archive"
        fi
        mkdir -p "$destination"
        tar -xzf "$archive" -C "$destination"
    fi
done < "$SRC/archives.txt"
if [ "${1:-}" != --record ]; then
    for manifest in sources.sha256 libc-sources.sha256; do
        if [ -f "$SRC/$manifest" ]; then
            (cd "$OUT/upstream" && sha256sum --check --status "$SRC/$manifest")
        fi
    done
fi
