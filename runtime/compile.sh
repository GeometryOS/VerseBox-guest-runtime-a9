#!/bin/sh
# Shared compile helpers for the API 28 guest runtime.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
RT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$RT/../.." && pwd)
UP="$RT/upstream"
OUT="$RT/build"
B="$UP/bionic/libc"
NDK=${ANDROID_NDK_HOME:-"C:/Users/22437/AppData/Local/Android/Sdk/ndk/29.0.14206865"}
if command -v cygpath >/dev/null 2>&1; then NDK=$(cygpath -u "$NDK"); fi
export PATH="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin:$PATH"
unset LD_LIBRARY_PATH LD_PRELOAD || true
CLANG_RESOURCE_DIR=$(clang -print-resource-dir)

compile() {
    input=$1
    object=$2
    shift 2
    case "$input" in
        */bionic/libc/*|*/bionic/libdl/*|*/filesystem/*) cxx_include="$UP/bionic/libstdc++/include"; cpp_atomic= ;;
        *) cxx_include="$UP/external/libcxx/include"; cpp_atomic=-D_USING_LIBCXX ;;
    esac
    set -- --target=aarch64-linux-android28 -nostdinc \
        -isystem "$cxx_include" \
        -isystem "$CLANG_RESOURCE_DIR/include" \
        -isystem "$B/include" -isystem "$B/kernel/uapi/asm-arm64" \
        -isystem "$B/kernel/uapi" -isystem "$B/kernel/android/uapi" \
        -I"$B" -I"$B/async_safe/include" -I"$B/private" -I"$RT/generated" \
        -I"$B/stdio" -I"$B/upstream-openbsd/android/include" \
        -I"$B/upstream-openbsd/lib/libc/include" \
        -I"$B/upstream-openbsd/lib/libc/stdio" -I"$B/upstream-openbsd/lib/libc/gdtoa" \
        -I"$B/upstream-freebsd/android/include" \
        -I"$B/upstream-netbsd/android/include" -I"$B/upstream-netbsd/lib/libc/include" \
        -I"$B/dns/include" -I"$B/tzcode" \
        -I"$OUT/derived/bionic/libc/system_properties/include" \
        -I"$OUT/derived/core/property_service/libpropertyinfoparser/include" \
        -I"$B/system_properties/include" \
        -I"$UP/bionic/linker" -I"$UP/system/core/base/include" \
        -I"$UP/system/core/liblog/include" -I"$UP/system/core/libutils/include" \
        -I"$UP/system/core/libcutils/include" -I"$UP/system/core/libsystem/include" \
        -I"$UP/system/core/include" -I"$UP/system/core/libziparchive/include" \
        -I"$UP/external/zlib" -I"$UP/external/zlib/src" \
        -I"$UP/external/libcxxabi/include" -I"$UP/external/jemalloc/include" \
        -I"$UP/system/core/property_service/libpropertyinfoparser/include" \
        -I"$MODULE/vendor/runtime/filesystem" \
        -fPIC -fno-stack-protector -fno-exceptions -fno-rtti -fno-sized-deallocation \
        -fno-emulated-tls -ffunction-sections -fdata-sections -mno-outline-atomics \
        -O2 -D_LIBC=1 $cpp_atomic -U_FORTIFY_SOURCE \
        -DLIBLOG_LOG_TAG=1006 -DSNET_EVENT_LOG_TAG=1397638484 \
        -MD -MF "$object.d" "$@"
    if [ "${BUILD_KIND:-}" = linker ]; then
        set -- "$@" -DLIBC_STATIC -fvisibility=hidden '-DLIB_PATH="lib64"'
    else
        set -- "$@" -DPLATFORM_SDK_VERSION=28 -D__BIONIC_LP32_USE_STAT64
    fi
    case "$input" in
        */upstream-openbsd/*|*/stdio/vfprintf.cpp|*/stdio/vfwprintf.cpp)
            set -- "$@" -include openbsd-compat.h ;;
        */upstream-freebsd/*) set -- "$@" -include freebsd-compat.h ;;
        # This must precede the generic upstream-netbsd pattern: POSIX case
        # takes the first match only.
        */upstream-netbsd/lib/libc/stdlib/*rand48.c|\
        */upstream-netbsd/lib/libc/stdlib/seed48.c|\
        */upstream-netbsd/lib/libc/stdlib/lcong48.c)
            set -- "$@" -include netbsd-compat.h -DPOSIX_MISTAKE -fcommon ;;
        */upstream-netbsd/*) set -- "$@" -include netbsd-compat.h -DPOSIX_MISTAKE ;;
        */dns/*) set -- "$@" -include netbsd-compat.h -DANDROID_CHANGES -DINET6 ;;
        */tzcode/*) set -- "$@" -DALL_STATE -DSTD_INSPIRED -DTHREAD_SAFE \
            -DTM_GMTOFF=tm_gmtoff '-DTZDIR="/system/usr/share/zoneinfo"' \
            -DHAVE_POSIX_DECLS=0 -DUSG_COMPAT=1 '-DWILDABBR=""' \
            -DNO_RUN_TIME_WARNINGS_ABOUT_YEAR_2000_PROBLEMS_THANK_YOU -Dlint ;;
        */bionic/fortify.cpp) set -- "$@" -D__BIONIC_DECLARE_FORTIFY_HELPERS -DNO___MEMCPY_CHK ;;
        */bionic/open.cpp) set -- "$@" -D__BIONIC_DECLARE_FORTIFY_HELPERS ;;
        */external/libcxxabi/src/*) set -- "$@" -DLIBCXXABI_SILENT_TERMINATE \
            -D_LIBCXXABI_BUILDING_LIBRARY -D_LIBCXXABI_NO_EXCEPTIONS ;;
        */system/core/liblog/*.c) set -- "$@" -fcommon ;;
        # AOSP 9's jemalloc is built without _GNU_SOURCE: bionic then declares
        # the POSIX strerror_r (int), which util.c expects.
        */external/jemalloc/src/*) set -- "$@" \
            -D_REENTRANT -DANDROID_MAX_ARENAS=2 \
            -DANDROID_LG_TCACHE_MAXCLASS_DEFAULT=16 \
            -DANDROID_TCACHE_NSLOTS_SMALL_MAX=8 \
            -DANDROID_TCACHE_NSLOTS_LARGE=16 -DJEMALLOC_TCACHE \
            -DANDROID_LG_CHUNK_DEFAULT=21 -fvisibility=hidden \
            -I"$UP/external/jemalloc/src" -I"$UP/external/jemalloc/include" \
            -include "$UP/bionic/libc/async_safe/include/async_safe/log.h" ;;
    esac
    case "$input" in
        */external/libcxx/src/*) clang++ "$@" -nostdinc++ -std=gnu++14 -D_LIBCPP_BUILDING_LIBRARY \
            -DLIBCXX_BUILDING_LIBCXXABI -c "$input" -o "$object" ;;
        */external/libcxxabi/src/*) clang++ "$@" -nostdinc++ -std=gnu++14 -c "$input" -o "$object" ;;
        *.cpp|*.cc) clang++ "$@" -nostdinc++ -std=gnu++17 -c "$input" -o "$object" ;;
        *.c) clang "$@" -std=gnu99 -c "$input" -o "$object" ;;
        *.S) clang "$@" -D__ASSEMBLY__ -c "$input" -o "$object" ;;
        *) printf 'Unsupported source: %s\n' "$input" >&2; exit 1 ;;
    esac
}
