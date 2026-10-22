#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SRC="$ROOT/container-probe/runtime/dynamic"
OUT="$ROOT/artifacts/container/runtime"
UP="$OUT/upstream"
B="$UP/bionic/libc"
for tool in clang clang++ ld.lld llvm-readelf; do command -v "$tool" >/dev/null; done
unset LD_LIBRARY_PATH LD_PRELOAD
test -f "$OUT/lib/libc.so"
test -f "$OUT/lib/libdl.so"
mkdir -p "$OUT/obj/dynamic-probe" "$OUT/bin"
BUILD_KIND=shared
. "$SRC/compile-platform.sh"
compile "$B/arch-common/bionic/crtbegin.c" "$OUT/obj/dynamic-probe/crtbegin.o"
compile "$B/arch-common/bionic/crtend.S" "$OUT/obj/dynamic-probe/crtend.o"
compile "$B/arch-common/bionic/crtbegin_so.c" "$OUT/obj/dynamic-probe/crtbegin_so.o"
compile "$B/arch-common/bionic/crtend_so.S" "$OUT/obj/dynamic-probe/crtend_so.o"
compile "$UP/external/libcxxabi/src/cxa_thread_atexit.cpp" "$OUT/obj/dynamic-probe/cxa_thread_atexit.o" \
    -DHAVE___CXA_THREAD_ATEXIT_IMPL
objects=
for path in "$SRC/test/DynamicProbe.cpp" "$SRC/test/AllocationProbe.cpp" "$SRC/test/LoadingProbe.cpp" \
    "$ROOT/container-probe/runtime/test/ThreadProbe.cpp" "$ROOT/container-probe/runtime/test/ProcessIdentity.cpp"; do
    object="$OUT/obj/dynamic-probe/$(basename "$path").o"
    compile "$path" "$object" -DA10VM_DYNAMIC_PROBE
    objects="$objects $object"
done
builtins=$(clang --target=aarch64-linux-android29 --print-libgcc-file-name)
ld.lld -pie -e _start -dynamic-linker /system/bin/linker64 \
    -z max-page-size=4096 -z now -z relro --hash-style=gnu --eh-frame-hdr --build-id=sha1 \
    --no-undefined --exclude-libs ALL -T "$ROOT/container-probe/runtime/arm64-tls.ld" \
    "$OUT/obj/dynamic-probe/crtbegin.o" $objects "$OUT/obj/dynamic-probe/cxa_thread_atexit.o" \
    "$OUT/lib/libc.so" "$OUT/lib/libdl.so" "$OUT/bin/source-linker64" "$builtins" \
    "$OUT/obj/dynamic-probe/crtend.o" -o "$OUT/bin/dynamic-probe"
compile "$SRC/test/TlsModule.cpp" "$OUT/obj/dynamic-probe/TlsModule.o"
ld.lld -shared -soname liba10vm-tls-test.so -z max-page-size=4096 -z now -z relro \
    --hash-style=gnu --eh-frame-hdr --build-id=sha1 --no-undefined --exclude-libs ALL \
    "$OUT/obj/dynamic-probe/crtbegin_so.o" "$OUT/obj/dynamic-probe/TlsModule.o" \
    "$OUT/obj/dynamic-probe/cxa_thread_atexit.o" "$OUT/lib/libc.so" "$builtins" \
    "$OUT/obj/dynamic-probe/crtend_so.o" -o "$OUT/lib/liba10vm-tls-test.so"
compile "$SRC/test/NamespaceModule.cpp" "$OUT/obj/dynamic-probe/NamespaceModule.o"
ld.lld -shared -soname liba10vm-namespace-test.so -z max-page-size=4096 -z now -z relro \
    --no-undefined "$OUT/obj/dynamic-probe/crtbegin_so.o" "$OUT/obj/dynamic-probe/NamespaceModule.o" \
    "$OUT/lib/libc.so" "$OUT/bin/source-linker64" "$builtins" \
    "$OUT/obj/dynamic-probe/crtend_so.o" -o "$OUT/lib/liba10vm-namespace-test.so"
llvm-readelf -hlWd "$OUT/bin/dynamic-probe" > "$OUT/dynamic-probe-elf.txt"
llvm-readelf -hlWd "$OUT/lib/liba10vm-tls-test.so" > "$OUT/dynamic-tls-elf.txt"
