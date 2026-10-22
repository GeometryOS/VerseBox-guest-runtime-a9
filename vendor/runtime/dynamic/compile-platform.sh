#!/bin/sh
# Sourced by the linker and shared-libc builders.
compile() {
    input=$1
    object=$2
    shift 2
    case "$input" in
        */bionic/libc/*|*/bionic/libdl/*) cxx_include="$UP/bionic/libstdc++/include"; cpp_atomic= ;;
        *) cxx_include="$UP/external/libcxx/include"; cpp_atomic=-D_USING_LIBCXX ;;
    esac
    set -- --target=aarch64-linux-android29 -nostdinc \
        -isystem "$cxx_include" \
        -isystem "$(clang -print-resource-dir)/include" \
        -isystem "$B/include" -isystem "$B/kernel/uapi/asm-arm64" \
        -isystem "$B/kernel/uapi" -isystem "$B/kernel/android/uapi" \
        -I"$B" -I"$B/async_safe/include" -I"$B/private" -I"$OUT/generated" \
        -I"$B/stdio" -I"$B/upstream-openbsd/android/include" \
        -I"$B/upstream-openbsd/lib/libc/include" \
        -I"$B/upstream-openbsd/lib/libc/stdio" -I"$B/upstream-openbsd/lib/libc/gdtoa" \
        -I"$B/upstream-freebsd/android/include" \
        -I"$B/upstream-netbsd/android/include" -I"$B/upstream-netbsd/lib/libc/include" \
        -I"$B/dns/include" -I"$B/tzcode" -I"${PROPERTY_INCLUDE:-$B/system_properties/include}" \
        -I"$UP/bionic/linker" -I"$UP/system/core/base/include" \
        -I"$UP/system/core/liblog/include" -I"$UP/system/core/libutils/include" \
        -I"$UP/system/core/libcutils/include" -I"$UP/system/core/libsystem/include" \
        -I"$UP/system/core/include" -I"$UP/system/core/libziparchive/include" \
        -I"$UP/external/zlib" -I"$UP/external/zlib/src" \
        -I"$UP/external/libcxxabi/include" -I"$UP/external/jemalloc_new/include" \
        -fPIC -fno-stack-protector -fno-exceptions -fno-rtti -fno-sized-deallocation \
        -fno-emulated-tls -ffunction-sections -fdata-sections -mno-outline-atomics \
        -O2 -D_LIBC=1 $cpp_atomic -U_FORTIFY_SOURCE \
        -DLIBLOG_LOG_TAG=1006 -DSNET_EVENT_LOG_TAG=1397638484 \
        -MD -MF "$object.d" "$@"
    if [ "$BUILD_KIND" = linker ]; then
        set -- "$@" -DLIBC_STATIC -fvisibility=hidden '-DLIB_PATH="lib64"'
    else
        set -- "$@" -DPLATFORM_SDK_VERSION=29 -D__BIONIC_LP32_USE_STAT64
    fi
    case "$input" in
        */upstream-openbsd/*|*/stdio/vfprintf.cpp|*/stdio/vfwprintf.cpp)
            set -- "$@" -include openbsd-compat.h ;;
        */upstream-freebsd/*) set -- "$@" -include freebsd-compat.h ;;
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
        */external/jemalloc_new/src/*) set -- "$@" \
            -I"$UP/external/jemalloc_new/src" -D_GNU_SOURCE -D_REENTRANT \
            -DANDROID_NUM_ARENAS=1 -DANDROID_TCACHE_NSLOTS_SMALL_MAX=2 \
            -DANDROID_TCACHE_NSLOTS_LARGE=1 -fvisibility=hidden -fno-strict-aliasing ;;
    esac
    case "$input" in
        */external/libcxx/src/*) python3 "$SRC/compile-source.py" "$object" clang++ "$@" \
            -nostdinc++ -std=gnu++14 -D_LIBCPP_BUILDING_LIBRARY \
            -DLIBCXX_BUILDING_LIBCXXABI -c "$input" -o "$object" ;;
        */external/libcxxabi/src/*) python3 "$SRC/compile-source.py" "$object" clang++ "$@" \
            -nostdinc++ -std=gnu++14 -c "$input" -o "$object" ;;
        *.cpp|*.cc) python3 "$SRC/compile-source.py" "$object" clang++ "$@" \
            -nostdinc++ -std=gnu++17 -c "$input" -o "$object" ;;
        *.c)
            case "$input" in
                */upstream-netbsd/lib/libc/stdlib/*rand48.c|*/upstream-netbsd/lib/libc/stdlib/seed48.c|*/upstream-netbsd/lib/libc/stdlib/lcong48.c)
                    set -- "$@" -fcommon ;;
            esac
            python3 "$SRC/compile-source.py" "$object" clang "$@" \
                -std=gnu99 -c "$input" -o "$object" ;;
        *.S) python3 "$SRC/compile-source.py" "$object" clang "$@" \
            -D__ASSEMBLY__ -c "$input" -o "$object" ;;
        *) printf 'Unsupported source: %s\n' "$input" >&2; exit 1 ;;
    esac
}
