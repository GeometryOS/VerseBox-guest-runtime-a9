#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/container-probe/properties"
OUT="$ROOT/artifacts/container/properties"
D="$OUT/derived"
for tool in git sha256sum; do command -v "$tool" >/dev/null; done
sh "$SRC/fetch-sources.sh"
mkdir -p "$D"
cp -R "$OUT/upstream/bionic" "$OUT/upstream/core" "$D/"
git apply --recount --unsafe-paths --directory="$D" "$SRC/platform.patch"
git apply --recount --unsafe-paths --directory="$D" "$SRC/runtime.patch"
