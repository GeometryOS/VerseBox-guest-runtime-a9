# A9 运行时构建说明（API 28）

本文说明 `runtime/` 下每个产物是什么、从哪来、按什么顺序构建、失败时先看哪里。
与 `STATUS.md` 的分阶段验收、`README.md` 的构建入口配套使用。

---

## 1. 为什么要有这一套

A10 的运行方式不是“直接跑 ROM 的 init”，而是由项目扮演 init，并用**项目源码构建的
linker/Bionic** 把客体绝对路径映射到应用私有目录：

- `A10VM_ROOTFS`：客体根文件系统；
- `A10VM_HOST_ROOT`：APK 原生库目录；
- `A10VM_STATE_ROOT`：属性/状态目录；
- `A10VM_LD_CONFIG`：私有 linker 配置；
- `A10VM_PROPERTIES`：私有属性区。

这些不是 A10 独有设计，而是所有客体平台都必须满足的容器约束。A9 不能直接使用
A10 的这套二进制（API 29 ABI），也不能直接使用 Redroid 9 的原版 libc（它不知道
这些私有路径）。因此必须在 **API 28 基线**上重建同一组组件：

```text
AOSP android-9.0.0_r61 + 项目文件系统/属性补丁
        ↓
source-linker64 / libc.so / libdl.so / libm.so（全部 API 28）
        ↓
Redroid 9 的 ART / framework / HAL / 系统 APK（原样保留）
```

---

## 2. 固定输入

| 仓库 | 参考 | 用途 |
| --- | --- | --- |
| bionic | `refs/tags/android-9.0.0_r61` | linker、libc、libdl、libm、libc++ 头 |
| system/core | 同上 | libbase、libutils、liblog、libcutils、libziparchive、属性解析器 |
| external/libcxx | 同上 | 头文件与 C++ 运行库源 |
| external/libcxxabi | 同上 | ABI 头/源 |
| external/zlib | 同上 | linker 的 zip 支持 |
| external/jemalloc | `f1df06c9b25685e61e991e254dfd9b91a9b58d5b` | libc 的分配器 |
| build/tools/fs_config | `android-9.0.0_r61` | AID 表生成 |

Redroid 9 客体：

```text
docker.io/redroid/redroid:9.0.0-240527
architecture: linux/arm64
manifest: sha256:ea740d3e3dbbdde88bf1229094a204eaf0eff83cb2bf10aab062278f29ac4c6b
layer:    sha256:8e95e590895f55a010e5e38e66d25c86f5e480c33a85093dcaa3439b4da86ad7
```

---

## 3. 构建顺序（有依赖，不能并行乱序）

```text
fetch-a9-sources.sh
        ↓
build-static.sh        生成 liba9vm-bootstrap.a
        ↓                （含 GuestPaths/GuestFileCalls/属性区补丁）
build-linker.sh        生成 bin/source-linker64
        ↓                （含 A9 缺失的“直接执行”模式）
build-libc.sh          生成 lib/libdl.so、lib/libc.so
        ↓
build-libm.sh          生成 lib/libm.so
```

每一步都要求前一步的产物存在，脚本会显式检查。

完整顺序（一键入口 `sh scripts/build.sh`，已有产物的阶段会跳过）：

| 阶段 | 命令 | 产物 |
| --- | --- | --- |
| 镜像 | `python scripts/fetch-redroid9.py`、`python scripts/extract-rootfs.py` | `downloads/`、`rootfs/` |
| 运行时源码 | `sh runtime/fetch-a9-sources.sh` | `runtime/upstream/` |
| 静态 Bionic | `sh runtime/build-static.sh` | `runtime/build/lib/liba9vm-bootstrap.a` |
| linker | `sh runtime/build-linker.sh` | `runtime/build/bin/source-linker64` |
| libc / libdl | `sh runtime/build-libc.sh` | `runtime/build/lib/libc.so`、`libdl.so` |
| libm | `sh runtime/build-libm.sh` | `runtime/build/lib/libm.so` |
| 属性桥 | `sh bridges/build-property.sh` | `bridges/lib/liba9vm-property-client.so` |
| Binder 桥 | `sh bridges/build-binder.sh` | `bridges/lib/liba9binder.so` |
| 设备桥 | `sh bridges/build-devices.sh` | `bridges/lib/liba9vm-guest-devices.so` |
| guest 日志 | `sh bridges/build-guest-log.sh` | `bridges/lib/liba9vm-guest-log.so` |
| 图形兼容层 | `sh bridges/build-graphics-a9.sh` | `bridges/graphics/lib/*.so` |
| prepared | `python scripts/prepare-a9.py` | `prepared/` |

依赖：Python 3、JDK、Android SDK（`platforms/android-35`、`build-tools/35.0.0`），
重建桥接与图形还需要 NDK；路径由 `ANDROID_HOME`、`ANDROID_NDK_HOME` 覆盖。

`prepare-a9.py` 的清单会顺带生成 `framework-check.dex`，因此改过
`bootstrap/*.java` 或 `bridges/` 产物后都要重跑这一步。

### 3.1 静态 Bionic（`build-static.sh`）

- 源清单：`lists/static-sources.txt`；
- 关键差异（相对 A10 r47）：A9 没有 `bionic_elf_tls.cpp`、
  `bionic_allocator.cpp`、`fdsan.cpp`；A9 用 `arch-arm64/bionic/__set_tls.c`；
- `GuestSyscalls.S` 接管 `syscall` / `prctl`，因此跳过 libc 的 `prctl.S`，
  并把 `syscall.S` 的符号改名为 `a10vm_kernel_syscall`；
- 属性区补丁对 A9 源码**原样应用成功**（`git apply --check` 通过）。

### 3.2 私有 linker（`build-linker.sh`）

- A9 原版 linker 只在被内核作为 PT_INTERP 加载时工作；
  `linker64 /absolute/program` 会打印“helper program”后退出；
- 已移植 A10 的 `load_executable()`：
  - `AT_ENTRY == _start` 时读取 `argv[1]`；
  - 打开绝对路径 ELF，`ElfReader::Read/Load`；
  - 自行建立 `soinfo`、`base`、`load_bias`、`entry_point`；
  - 去掉 loader 自身的 argv 项；
- 产物验收：`llvm-nm -u` 为空，`DT_NEEDED` 为空，SONAME 为 `ld-android.so`。

### 3.3 共享 libc/libdl（`build-libc.sh`）

- 源清单由 `select-libc-sources.py` 解析 A9 `bionic/libc/Android.bp` 生成，
  当前 715 个 ARM64 源文件；
- 版本脚本由 A9 自带的 `genversion-scripts.py` 生成（A9 没有 A10 的
  `generate-version-script.py`）；
- jemalloc 需要 A9 自己的编译参数（`-DANDROID_MAX_ARENAS=2`、
  `-include .../async_safe/log.h` 等）；
- `libdl.so` 先链接，`libc.so` 再链接到 `libdl.so` + `source-linker64`。

### 3.4 libm（`build-libm.sh`）

- 源清单来自 `bionic/libm/Android.bp` 的 `libm` 模块（229 个源）；
- 链接需要 API 28 的 `libc.so`；
- 不重建 `libc++/libunwind`：这两个由 Redroid 9 原样提供，只要求它们与 API 28
  libc 的公开 ABI 兼容（阶段 1 的符号闸门会验证）。

---

## 4. 产物与验收

| 产物 | 位置 | 验收 |
| --- | --- | --- |
| 静态 Bionic | `runtime/build/lib/liba9vm-bootstrap.a` | `llvm-nm` 无 A10 符号 |
| 私有 linker | `runtime/build/bin/source-linker64` | 无未定义符号、无 DT_NEEDED、AArch64 |
| libdl | `runtime/build/lib/libdl.so` | 导出 `android_set_application_target_sdk_version@@LIBC_PLATFORM` |
| libc | `runtime/build/lib/libc.so` | 含 `A10VM_ROOTFS` 等映射逻辑、属性区补丁 |
| libm | `runtime/build/lib/libm.so` | 无未定义符号，依赖 libc.so |

符号闸门（阶段 1 退出条件）：

```bash
# A9 libart 的未定义符号必须全部由 A9 域 libc/libdl/libm/libart 等提供
llvm-readelf --dyn-syms rootfs/system/lib64/libart.so
```

---

## 5. 失败时先看哪里

| 现象 | 首先检查 |
| --- | --- |
| `Unsupported source` | 源清单是否混入 CRLF / 拼错路径 |
| `fatal error: 'xxx' file not found` | 该版本 AOSP 是否把文件移到了别的目录 |
| `undefined symbol`（链接） | 静态/共享源清单缺文件，或版本脚本要求了未编入的符号 |
| `version script assignment ... failed` | 版本脚本包含未编入符号；先确认对象是否在链接行，再考虑 `--undefined-version` |
| jemalloc 编译错误 | 是否带上了 A9 的 `-include async_safe/log.h` 和 Android 参数 |
| ART 阶段缺符号 | 该符号属于哪个 API 域；不要用 shim 掩盖 |

---

## 6. 不变量

1. A9 产物只允许来自 API 28 源码或 Redroid 9 原文件；
2. `liba9vm-*` 与 `liba10vm-*` 不交叉；
3. 路径/属性/身份映射只由源码 libc/linker 提供；
4. 任何 helper 进程不得把自己的库预加载进客体进程；
5. ART 自检不过，不启动系统服务。
