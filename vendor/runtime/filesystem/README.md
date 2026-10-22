# 免 root 客体路径与 exec 探针

2026-10-08：在普通 Termux UID 10373 下通过 29 项检查及四项判定负测。
配套 Android 10 linker/Bionic 把客体路径映射到当前应用的私有目录，
无需宿主挂载、`chroot` 或 `su`。新探针 APK 为 `g1-rootless-filesystem` /
versionCode 11，构建、Lint、签名和 14 个 ELF 的内容核对通过。
本轮没有 ADB 设备连接，APK UID 下的运行仍待验收。

## 构建与复现

在项目根目录执行，复用现有只读 v4 镜像和已固定的 Android 10 源码：

```sh
sh container-probe/runtime/filesystem/prepare-apk.sh \
  artifacts/guest/android10-display-v4.raw \
  artifacts/container/gate-0-repeatable/report.json
python3 container-probe/runtime/filesystem/test-native.py
python3 container-probe/runtime/test-native.py
python3 container-probe/runtime/dynamic/test-native.py
sh tools/build-termux.sh :container-probe:assembleDebug :container-probe:lintDebug \
  -PcontainerProbe -PcontainerFilesystem

# 已连接 ADB 开发设备时：安装、普通 APK UID 运行、回收报告与卸载测试包。
sh container-probe/test-device.sh --filesystem
```

本轮唯一探针归档为 `artifacts/container/runtime/filesystem/probe.apk`，
2,581,083 字节；v2 签名通过，Lint 为 0 errors / 8 warnings。
五份包内输入清单的所有 ELF 大小与 SHA-256 均匹配，原生文件闭集为 14 个。
本地报告位于 `artifacts/container/runtime/filesystem/local-report.json`，
精简证据见[文件系统清单](../../../docs/evidence/container-runtime-filesystem.json)。

## 职责划分与已验证语义

- `GuestPaths`：客体绝对路径、cwd/dirfd 相对路径、逐段符号链接解析；循环链接返回
  `ELOOP`，客体缺少 `/system/etc/hosts` 时返回 `ENOENT`。
- `GuestFileCalls`：将 open/access/stat/readlink/chdir/getcwd、目录及文件修改调用
  接到路径映射，读写落在私有 rootfs。
- `GuestExec`：识别客体 ARM64 PIE，通过配套源码 linker 启动子进程，继承私有配置
  与属性区，保留调用方 `argv[0]` 和 guest cwd。
- `sources.sh`：共享替换 syscall 源的清单，供静态 Bionic、linker 与动态 libc 构建。
- `FilesystemRuntimeProbe` / `verify.py`：分别在 APK 与本地逐进程核验身份和映射；
  复用现有配置、属性、进程管理和输入校验组件。

29 项检查覆盖绝对/相对路径、absolute/relative symlink、APEX 目录别名、
`..`、dirfd、私有写入、rename/unlink/mkdir/rmdir、cwd、通用客体路径调用
`fork`/`execve`、子进程 SDK 29、argv0 及退出状态。

父、子均分别满足：UID/eUID 10373、有效能力全零、`untrusted_app_27`，
文件型可执行映射精确匹配源码 linker、程序、libc/libdl。每进程三种私有属性文件
各有两份 `r--s` / 偏移 0 映射。判定器对错误子进程 eUID、非零能力、
缺 SELinux 标签与宿主 libc 映射四种修改记录均判失败。

当前运行库的静态自举四场景、动态属性四场景及装载失败四场景同轮通过，
未设置 `A10VM_ROOTFS` 的旧运行库调用回归通过。`renameat` 曾因缺少 C 声明
产生 C++ 符号，补入 `<stdio.h>` 后版本脚本链接与真实 rename 检查通过。

## 环境与下一关

`A10VM_ROOTFS` 指定私有 guest root；`A10VM_HOST_ROOT` 指定已核对的程序/运行库，
`A10VM_STATE_ROOT` 指定属性和配置数据，`A10VM_EXEC_LINKER` 指定配套 loader。
`A10VM_PROPERTIES` / `A10VM_LD_CONFIG` 复用属性与动态装载配置。

本模块目前仅适配所列 libc 调用和 ARM64 PIE 子进程链；APEX 验收对象是目录
符号链接。脚本执行、其他路径 syscall、完整 APEX 激活、虚拟 UID、ART/系统服务
及 Android 10 桌面继续接入。宿主权限一直为普通应用 UID；完整 G1 和桌面尚未验收。
探针执行耗时不作为冷启动、FPS 或点击延迟指标。

KISS/YAGNI：用现有小程序证明当前语义，保留原镜像，避免生成新整盘副本。
SRP/SOLID：路径解析、文件调用、进程执行与结果核验分别成模块。
DRY：三种运行库构建共用源码替换清单，复用属性、运行与回收链。
