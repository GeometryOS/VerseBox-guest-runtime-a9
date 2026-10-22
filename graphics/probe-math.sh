#!/bin/sh
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
url="https://android.googlesource.com/platform/bionic/+/refs/tags/android-9.0.0_r61/libc/include/math.h?format=TEXT"
curl -s --max-time 30 "$url" | base64 -d > /tmp/a9-math.h
echo "lines: $(wc -l < /tmp/a9-math.h)"
grep -n "__cplusplus\|signbit\|isfinite\|isnormal" /tmp/a9-math.h | head -20
