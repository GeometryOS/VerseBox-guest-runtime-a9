#!/bin/sh
# Build the API 28 binder transport bridges:
#   liba9binder.so             project binder wire/driver core
#   binder-broker              independent broker (host-style helper)
#   liba9vm-binder-devices.so  LD_PRELOAD shim that redirects /dev/binder
# A9 keeps the ROM's libbinder/libhwbinder; only the transport is project code.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
. "$(dirname -- "$0")/compile.sh"

SRC="$MODULE/vendor/binder/native"
OBJ="$OUT/obj/binder"
mkdir -p "$OBJ" "$OUT/lib" "$OUT/bin"
test -f "$RT/build/lib/libc.so" || { echo "run build-libc.sh first" >&2; exit 1; }

crt_so
crt_pie
builtins=$(clang --target=$TARGET --print-libgcc-file-name)
COMPAT="-include $MODULE/bridges/compat/binder_compat.h"

# Project transport core. Ashmem.cpp / Scheduling.cpp replace libcutils pieces,
# so the library only depends on libc/libc++.
# NOTE: these sources deliberately contain no thread_local variables. A9's
# linker has no R_AARCH64_TLSDESC handler and its R_AARCH64_TLS_TPREL64 branch
# is a trace-only no-op, so any shared-library TLS relocation would either
# jump to 0 or corrupt the main thread pointer. Per-thread state uses pthread
# keys instead (see Driver.cpp / Wire.cpp / BinderDevices.cpp).

binder_objects=
for path in Wire.cpp TransactionLayout.cpp TransactionWire.cpp Driver.cpp \
            ClientBuffers.cpp Ashmem.cpp Scheduling.cpp; do
    object="$OBJ/$(basename "$path").o"
    platform_compile "$SRC/$path" "$object" $COMPAT
    binder_objects="$binder_objects $object"
done
ld.lld -shared -soname liba9binder.so -z now -z relro -z max-page-size=4096 \
    --hash-style=gnu --eh-frame-hdr --build-id=sha1 --no-undefined --exclude-libs ALL \
    "$OUT/obj/crt/crtbegin_so.o" $binder_objects \
    "$LIBCXX" "$LIBC" "$LIBM" "$LIBDL" "$builtins" \
    "$OUT/obj/crt/crtend_so.o" -o "$OUT/lib/liba9binder.so"
llvm-readelf -hlWd "$OUT/lib/liba9binder.so" > "$OUT/liba9binder.elf.txt"

# Guest-side /dev/binder redirect shim. No thread_local here either: the
# inside-driver recursion guard is a pthread key (see BinderDevices.cpp).
platform_compile "$MODULE/vendor/native/BinderDevices.cpp" "$OBJ/binder-devices.o" $COMPAT
ld.lld -shared -soname liba9vm-binder-devices.so -z now -z relro -z max-page-size=4096 \
    --no-undefined "$OUT/obj/crt/crtbegin_so.o" "$OBJ/binder-devices.o" \
    "$OUT/lib/liba9binder.so" "$LIBC" "$OUT/obj/crt/crtend_so.o" \
    -o "$OUT/lib/liba9vm-binder-devices.so"

# Broker: one independent process per guest, talks the same wire protocol.
# ServiceRegistry.cpp is a separate optional registry binary (needs the A9
# libbinder headers); the first boot uses the ROM servicemanager instead.
broker_objects=
for path in Broker.cpp BrokerCleanup.cpp BrokerDispatch.cpp BrokerMain.cpp \
            BrokerNodeReclaim.cpp BrokerObjects.cpp BrokerTransactions.cpp \
            Wire.cpp; do
    object="$OBJ/broker-$(basename "$path").o"
    platform_compile "$SRC/$path" "$object" $COMPAT
    broker_objects="$broker_objects $object"
done
ld.lld -pie -e _start -dynamic-linker /system/bin/linker64 \
    -z now -z relro -z max-page-size=4096 --no-undefined --exclude-libs ALL \
    "$OUT/obj/crt/crtbegin.o" $broker_objects \
    "$OUT/lib/liba9binder.so" \
    "$MODULE/rootfs/system/lib64/libbase.so" \
    "$MODULE/rootfs/system/lib64/liblog.so" \
    "$MODULE/rootfs/system/lib64/libcutils.so" \
    "$MODULE/rootfs/system/lib64/libutils.so" \
    "$LIBCXX" "$LIBC" "$LIBM" "$LIBDL" "$LINKER" "$builtins" \
    "$OUT/obj/crt/crtend.o" -o "$OUT/bin/binder-broker"

for f in "$OUT/lib/liba9binder.so" "$OUT/lib/liba9vm-binder-devices.so" "$OUT/bin/binder-broker"; do
    llvm-readelf -hlWd "$f" > "$f.elf.txt"
done
echo "binder: $OUT/lib/liba9binder.so"
echo "binder: $OUT/lib/liba9vm-binder-devices.so"
echo "binder: $OUT/bin/binder-broker"
