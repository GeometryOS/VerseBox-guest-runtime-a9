<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/logo-dark.png">
    <img src="assets/logo.png" alt="guest-runtime-a9" width="96">
  </picture>
</p>

<p align="center">
  <a href="LICENSES/AGPL-3.0-only.txt"><img src="https://img.shields.io/badge/runtime-AGPL--3.0-blue.svg" alt="runtime: AGPL-3.0"></a>
  <a href="LICENSES/MIT.txt"><img src="https://img.shields.io/badge/tooling-MIT-green.svg" alt="tooling: MIT"></a>
  <a href="https://hub.docker.com/r/redroid/redroid"><img src="https://img.shields.io/badge/guest%20image-Redroid%209.0.0--240527-6f42c1.svg" alt="guest image: Redroid 9.0.0-240527"></a>
</p>

# guest-runtime-a9

这是一个可以在不用 root，不用 Docker，不用容器的情况下，让 **Android 9（API 28）** 运行在 VerseBox 虚拟机的安卓9独立客体😋（VerseBox 后续也将开源？）

> **目前的状态：可以启动并操作。**
> 音频桥、media 与 `host-devices` 尚未接入；导致启动后需要卡顿一段时间才能操作的 dexopt 仍在修复。

## 这是干什么的

Android 9 的镜像通常依赖容器或 root 环境运行。本仓库自行搭建运行所需的基础设施：
一份 API 28 的 linker/libc/libdl/libm，一套将客体绝对路径映射到 App 私有目录的补丁，
以及 Binder、属性、图形三座桥。用户空间复用 Redroid 9（ART、framework、系统应用、HAL），
构成一个不依赖宿主系统版本、也不需要特权的客体。


## 构建

```sh
sh scripts/build.sh
```

脚本会按需跳过已完成阶段喵，详细的顺序与产物的验收请见 [BUILD.md](BUILD.md)。
如需打包请在宿主工程中完成：`.\gradlew :app:assembleDebug`
（Gradle 侧等价入口：`.\gradlew :guest-runtime-a9:prepareA9Runtime`）。

需要 Python 3、JDK、Android SDK（`platforms/android-35`、`build-tools/35.0.0`），
重建桥接与图形还需要 NDK，路径通过 `ANDROID_HOME` 与 `ANDROID_NDK_HOME` 读取。

## 运行原理🤔

宿主 App 承担 init 的角色：读取清单，按 uid/gid 启动每个客体进程。
客体内的 `/system`、`/data` 等绝对路径由自建 linker/libc 重定向到本地目录，
内核不会访问宿主系统的对应路径：

| 层 | 来自 | 职责 |
| --- | --- | --- |
| 启动器 | 宿主工程 `FrameworkSession` | 读取清单、启动进程、收集日志与就绪标记 |
| 运行时 | `runtime/` | API 28 的 linker/libc/libdl/libm，含路径映射与私有属性区补丁 |
| 桥接 | `bridges/` | 属性服务、Binder/hwbinder 传输、设备、guest 日志、`netd`/`mdns` 监听端 |
| 图形 | `graphics/`、`bridges/graphics/` | goldfish EGL/GLES、gralloc、HWC，帧经 `rcFBPost`/`rcCompose` 交给宿主合成 |
| 用户空间 | Redroid 9 镜像 | ART、framework、系统服务、系统 APK、HAL |
| 汇总 | `prepared/` | 以上全部打包为 assets、jniLibs 与清单 |

这是一次冷启动的时序：

| 时刻 | 事件 |
| --- | --- |
| 0s | `property-service`、`binder-broker`、`servicemanager`、`hwservicemanager` 启动 |
| ~1s | 图形 HAL 注册，SurfaceFlinger 开屏 |
| ~4s | `A9Boot` 进入 `SystemServer` |
| ~6s | `startOtherServices`、`systemReady`、`sys.boot_completed=1` |
| ~7s | Launcher3 首帧，输入通道就绪 |

## 目录

```text
scripts/     prepare-a9.py、镜像抓取/解包、闭包审计
bootstrap/   A9Boot、A9DesktopCheck、A9AudioPolicy 等 Java 启动层
bridges/     属性/Binder/设备/日志/netd 监听/图形 HAL 源码与构建脚本
graphics/    goldfish 图形源码、emugen 定义、AOSP 9 头文件
runtime/     API 28 linker/libc/libdl/libm 的源列表、补丁、构建脚本
native/      A9 专用 native 源码
vendor/      共享容器源码副本，保证本仓库可独立 clone 构建
prepared/    产物：assets + jniLibs（不入库）
rootfs/      解包后的 Redroid 9 根文件系统（不入库）
```

## 注意事项

- **镜像文件不在本仓库。** AOSP9 的 tar 仅保留在本地工作区，
  仓库只包含构建其的补丁源代码。

- **判定依据为来源与哈希，而非文件名** 清单中每条 native
  均记录 `source` 与 `sha256`，`verifyRuntimeA9` 在构建时逐条核对。

## 文档

- 构建、产物验收、排查：[BUILD.md](BUILD.md)
- 分阶段状态：[STATUS.md](STATUS.md)

## 许可证

本仓库按目录拆分许可：

- **核心客体运行时**（`runtime/`、`bridges/`、`bootstrap/`、`graphics/` 的项目补丁、
  `native/`）：**AGPL-3.0-only**（[全文](LICENSES/AGPL-3.0-only.txt)）。
  对外提供修改后的客体时，需按 AGPL §13 向使用者提供对应源码；
  个人自用、自行修改不受影响。

- **构建与集成工具**（`scripts/`、清单生成、宿主侧接入的配置与示例）：
  **MIT**（[全文](LICENSES/MIT.txt)）。可自由接入自有应用。

- **`vendor/`、`graphics/framework/`**：来自 AOSP `android-9.0.0_r61` 与宿主工程的
  共享容器层，保留 **Apache-2.0** 及文件自带的 BSD/MIT 声明，作为独立部分分发。

Redroid 9 镜像在构建时获取，不随仓库分发，镜像自身许可以上游为准。
根目录 [LICENSE](LICENSE) 仅为拆分范围的简短说明。
