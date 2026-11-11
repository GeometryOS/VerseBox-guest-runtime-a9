#!/bin/sh
# One-shot build: image -> API 28 runtime -> bridges -> prepared.
#
# Every stage is skipped when its product already exists.  To force a rebuild,
# delete that stage's output (rootfs/, runtime/build/, bridges/lib/, prepared/).
# The per-stage order and acceptance criteria live in BUILD.md.
set -eu
: "${MODULE:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
cd "$MODULE"

step() { printf '\n== %s\n' "$1"; }
exists() { [ -e "$1" ]; }

# 只影响最后两步（ROM 包 / APK payload）；前面的镜像、运行时、桥、prepared 不变。
SKIP_ROM=0
LIB_MODE=
while [ $# -gt 0 ]; do
    case "$1" in
        --skip-rom) SKIP_ROM=1 ;;
        --rom-version) shift; ROM_VERSION=${1:?--rom-version 需要参数} ;;
        --rom-out) shift; ROM_OUT=${1:?--rom-out 需要参数} ;;
        --lib-mode) shift; LIB_MODE=${1:?--lib-mode 需要参数} ;;
        -h|--help)
            printf 'usage: build.sh [--skip-rom] [--rom-version X.Y.Z] [--rom-out PATH] [--lib-mode apk|rom|both]\n'
            exit 0 ;;
        *) printf 'unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

if ! ls downloads/*.tar.gz >/dev/null 2>&1; then
    step "fetch Redroid 9 image"
    python scripts/fetch-redroid9.py
fi

if ! exists rootfs/system/bin/app_process64; then
    step "extract rootfs"
    python scripts/extract-rootfs.py
fi

if ! exists runtime/build/lib/libc.so; then
    step "runtime sources (AOSP android-9.0.0_r61)"
    sh runtime/fetch-a9-sources.sh
    step "static bionic"
    sh runtime/build-static.sh
    step "linker"
    sh runtime/build-linker.sh
    step "libc / libdl"
    sh runtime/build-libc.sh
    step "libm"
    sh runtime/build-libm.sh
fi

if ! exists bridges/lib/liba9vm-property-client.so; then
    step "property bridge"
    sh bridges/build-property.sh
    step "binder bridge"
    sh bridges/build-binder.sh
    step "device bridge"
    sh bridges/build-devices.sh
    step "guest log"
    sh bridges/build-guest-log.sh
fi

if ! exists bridges/graphics/lib/libEGL_emulation.so; then
    step "graphics compatibility layer"
    sh bridges/build-graphics-a9.sh
fi

step "prepared (assets + jniLibs + framework-check.dex)"
python scripts/prepare-a9.py

# ---------------------------------------------------------------- ROM 包与 APK payload
# 两条产物链，别混（docs/rom-package/11 §2）：
#   * ROM 包（新格式 zip）只依赖 prepared；
#   * APK 的 payload（旧格式，客体 .so 在 assets/<prefix>/lib/**）由 stager 产出，
#     Gradle 消费的是 prepared-apk，不是 prepared。
# 打包器与 stager 都在**本仓库**的 rom/ 里：A9 / A10 / A15 是三个独立仓库，
# 脚本不跨仓库引用（否则"只 clone A9"就打不出 A9 的 ROM）。
PACK_ROM=rom/pack-rom.py
STAGE_PAYLOAD=rom/stage-apk-payload.py
MANIFEST=guest-runtime-a9-inputs.json
PREFIX=guest-runtime-a9/
ROM_ID=$(sed -n 's/^id:[[:space:]]*//p' rom/rom.properties.yaml | head -n 1)
ROM_VERSION=${ROM_VERSION:-$(sed -n 's/^defaultRomVersion:[[:space:]]*//p' rom/rom.properties.yaml | head -n 1)}
ROM_OUT=${ROM_OUT:-dist/versebox-$ROM_ID-$ROM_VERSION.zip}

for tool in "$PACK_ROM" "$STAGE_PAYLOAD"; do
    if [ ! -f "$tool" ]; then
        printf '\n! 缺少本仓库的 ROM 工具：%s（源码应随仓库带过来）\n' "$tool" >&2
        exit 1
    fi
done

if [ "${SKIP_ROM:-0}" = 1 ]; then
    step "ROM package: skipped (--skip-rom)"
else
    step "ROM package ($ROM_OUT)"
    # set -- 是 POSIX 里唯一能安全拼参数的办法（路径可能带空格，不能用字符串展开）。
    # 注意：set -e 下不许写 `cond && set --`——条件为假时整条命令返回 1，脚本会直接退出。
    set -- --prepared prepared --properties rom/rom.properties.yaml --out "$ROM_OUT"
    if [ -n "$LIB_MODE" ]; then set -- "$@" --lib-mode "$LIB_MODE"; fi
    GIT_REV=$(git rev-parse --short HEAD 2>/dev/null || true)
    if [ -n "$GIT_REV" ]; then set -- "$@" --git-rev "$GIT_REV"; fi
    python "$PACK_ROM" "$@"
    step "ROM package self-check"
    python "$PACK_ROM" --verify-only "$ROM_OUT"
fi

step "APK payload (old format: assets/$PREFIX/lib/**)"
python "$STAGE_PAYLOAD" --prepared prepared --out prepared-apk \
    --manifest "$MANIFEST" --prefix "$PREFIX"

printf '\nbuild finished\n  prepared     : %s/prepared\n  APK payload : %s/prepared-apk\n  ROM package : %s\n' \
    "$MODULE" "$MODULE" "$( [ "${SKIP_ROM:-0}" = 1 ] && echo '（已跳过）' || echo "$MODULE/$ROM_OUT" )"
