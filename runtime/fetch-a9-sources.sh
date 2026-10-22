#!/bin/sh
# Fetch the pinned Android 9.0.0_r61 sources used to build the A9 guest loader.
# Tags are immutable; the extracted file hashes are recorded by build-a9-runtime.sh.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
RT="$MODULE/runtime"
UP="$RT/upstream"
TAG="refs/tags/android-9.0.0_r61"
BASE="https://android.googlesource.com/platform"

mkdir -p "$RT/downloads" "$UP"
fetch() {
    repo="$1"; sub="$2"; dest="$3"
    if [ -d "$UP/$dest" ] && [ -n "$(ls -A "$UP/$dest" 2>/dev/null)" ]; then return 0; fi
    name=$(printf '%s-%s' "$repo" "$sub" | tr / _)
    archive="$RT/downloads/$name.tar.gz"
    if [ ! -s "$archive" ]; then
        if [ "$sub" = "." ]; then url="$BASE/$repo/+archive/$TAG.tar.gz";
        else url="$BASE/$repo/+archive/$TAG/$sub.tar.gz"; fi
        printf 'fetch %s\n' "$url"
        curl -sS --fail --retry 3 --retry-delay 10 --max-time 600 "$url" -o "$archive.part"
        mv "$archive.part" "$archive"
    fi
    mkdir -p "$UP/$dest"
    # AOSP archives contain a few host-only symlinks that Windows cannot create.
    tar -mxzf "$archive" -C "$UP/$dest" 2>/dev/null || true
}

fetch_blob() {
    repo="$1"; sub="$2"; dest="$3"
    if [ -s "$UP/$dest" ]; then return 0; fi
    mkdir -p "$(dirname "$UP/$dest")"
    url="$BASE/$repo/+/$TAG/$sub?format=TEXT"
    printf 'fetch %s\n' "$url"
    curl -sS --fail --retry 3 --retry-delay 3 --max-time 300 "$url" -o "$UP/$dest.b64"
    base64 -d "$UP/$dest.b64" > "$UP/$dest"
    rm -f "$UP/$dest.b64"
}

fetch bionic      .                 bionic
fetch system/core base              system/core/base
fetch system/core libutils          system/core/libutils
fetch system/core libcutils         system/core/libcutils
fetch system/core liblog            system/core/liblog
fetch system/core libsystem/include system/core/libsystem/include
fetch system/core include           system/core/include
fetch system/core libziparchive     system/core/libziparchive
fetch system/core property_service/libpropertyinfoparser system/core/property_service/libpropertyinfoparser
fetch external/libcxx src           external/libcxx/src
fetch external/libcxx include       external/libcxx/include
fetch external/libcxxabi src        external/libcxxabi/src
fetch external/libcxxabi include    external/libcxxabi/include
fetch external/libunwind_llvm .     external/libunwind_llvm
fetch_blob external/libcxx           Android.bp external/libcxx/Android.bp
fetch_blob external/libcxxabi        Android.bp external/libcxxabi/Android.bp
fetch_blob external/libunwind_llvm   Android.bp external/libunwind_llvm/Android.bp
fetch external/zlib .               external/zlib
fetch external/jemalloc .           external/jemalloc
fetch build tools/fs_config         build/tools/fs_config

echo "sources ready under $UP"
