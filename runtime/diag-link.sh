: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
#!/bin/sh
# Temporary bisect harness for the libc link failure. Safe to delete.
set -u
cd /d/my123/VerseBox-master
TOOL=/c/Users/22437/AppData/Local/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin
export PATH="$TOOL:$PATH"
builtins=$(clang --target=aarch64-linux-android28 --print-libgcc-file-name)
RSP="@$MODULE/runtime/build/obj/libc/objects.rsp"
EXTRA="$MODULE/runtime/build/lib/libdl.so $MODULE/runtime/build/bin/source-linker64 $builtins"
VS="--version-script $MODULE/runtime/build/generated/libc.map"

run() {
  label=$1
  shift
  echo "=== $label ==="
  # shellcheck disable=SC2086
  "$TOOL/ld.lld" -shared -soname libc.so $VS "$@" $RSP $EXTRA \
    -o $MODULE/runtime/build/test4.so > $MODULE/runtime/build/link4.log 2>&1
  code=$?
  echo "EXIT=$code"
  echo "version-errors=$(grep -c 'version script assignment' $MODULE/runtime/build/link4.log || true)"
  head -3 $MODULE/runtime/build/link4.log
}

run "baseline"
run "no-undefined" --no-undefined
run "exclude-libs" --exclude-libs ALL
run "both" --no-undefined --exclude-libs ALL
run "z-flags" -z max-page-size=4096 -z now -z relro
run "hash-flags" --hash-style=gnu --eh-frame-hdr --build-id=sha1
run "error-limit" --error-limit=0
run "map" -Map $MODULE/runtime/build/libc-test.map
