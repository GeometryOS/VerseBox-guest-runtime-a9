# Android 9 客体移植 · 真实进度与阶段验收

最后更新：2026-10-23 12:3x（真机验证：日志转发 ✅、电池显示 42% 🟡、
卡顿 9.2s→1.6s、dexopt 仍 74 次；完整交接见主工程 26 章）

> **先读主工程 `docs/android9-port/26-真实进度与交接.md`。** 本文只记产物与阶段验收。

> 想看“现在到哪了、卡在哪、下一步做什么”，先读
> 主工程的 `docs/android9-port/15-当前进度与卡点.md`（主工程）。
> 本文记录各阶段的产物与验收证据。

本文只记录**已经完成并有产物/哈希可查**的工作，以及**下一道硬闸门**。
没有通过的阶段不写“已完成”。

---

## 0. 先对齐 A10 的真实来源，再谈 A9

A10 客体不是“把 AOSP 编成 ROM”，而是两部分拼起来：

| 部分 | A10 实际来源 | 在本项目里的角色 |
| --- | --- | --- |
| 客体 ROM 用户空间 | **Redroid `10.0.0-250907` arm64** rootfs | ART、framework、系统服务、系统 APK、HAL |
| 项目自建运行时组件 | **AOSP `android-10.0.0_r47`** 源码 | linker、libc/libdl/libm、属性核心、路径/身份/设备补丁、Binder/图形桥 |

证据：`_bin/A10VM.apk` 的 `guest.prop` 中 `ro.product.device=redroid_arm64`；
项目 `container-probe/runtime/dynamic`、`filesystem`、`properties` 的源码固定为
`android-10.0.0_r47`。A10 prepared 里的 `liba10vm-guest-libc.so` 确实包含
`A10VM_ROOTFS` / `A10VM_HOST_ROOT` / `A10VM_STATE_ROOT` / `A10VM_LD_CONFIG`
字符串，说明容器路径映射发生在**项目源码 libc/linker**里，而不是宿主 Java 层。

A9 的对应关系必须逐项相同：

| 部分 | A9 目标来源 |
| --- | --- |
| 客体 ROM 用户空间 | **Redroid `9.0.0-240527` arm64** rootfs（已下载并校验） |
| 项目自建运行时组件 | **AOSP `android-9.0.0_r61`** 源码（与 A10 的 r47 同角色） |

所以 A9 的正确路线不是“把 A10 的 linker/libc 改个名字”，也不是“拿 A9 ROM 原版
linker 硬跑”，而是：**同一套项目源码补丁，在 API 28 基线上重新构建一份运行时**。
这与 `docs/zh-CN/03-ROM构建原理.md` §4.1、§7.1、§12、§14.3、§14.7 的表述一致。

---

## 1. 阶段划分与当前状态

### 阶段 0 · 客体材料（已完成）

- Redroid 9 镜像：`docker.io/redroid/redroid:9.0.0-240527`，`linux/arm64`；
  manifest `sha256:ea740d3e...`，层 `sha256:8e95e590...`。
- `downloads/9.0.0-240527-0.tar.gz`，664,486,463 字节。
- rootfs 解包 1.5 GiB，`rootfs-index.json` 记录 3,741 个路径。
- 已核对：SDK 28、无 APEX、ART 在 `/system`、`Launcher3QuickStep`、
  `init.environ.rc` 的 BOOTCLASSPATH / SYSTEMSERVERCLASSPATH。
- App 侧已有 `GuestPlatform`、`guest-runtime-a9` 模块、主页 Android 9 卡片、
  独立 `a9-state/` 目录与 socket 根。

### 阶段 1 · API 28 运行时闭包（已完成，2026-10-22 03:2x）

固定源码：AOSP `refs/tags/android-9.0.0_r61`，已落盘到：
`bionic`、`system/core`、`external/libcxx`、`external/libcxxabi`、
`external/libunwind_llvm`、`external/zlib`、`external/jemalloc`、
`build/tools/fs_config`。

已完成并有产物：

1. **静态 Bionic 归档** `runtime/build/lib/liba9vm-bootstrap.a`
   - 由 `runtime/build-static.sh` 生成；
   - 包含 libc 静态闭包、项目 `GuestPaths/GuestFileCalls/...` 路径补丁、
     私有属性区补丁（`container-probe/properties/{platform,runtime}.patch`
     对 A9 源码原样应用成功）；
   - syscall 层：`GuestSyscalls.S` 接管 `syscall`/`prctl`，与 A10 相同的
     `a10vm_kernel_syscall` 命名。
2. **API 28 私有 linker（源码）**
   - `runtime/build-linker.sh`；
   - A9 原版 linker **没有**“linker64 /绝对路径/程序”直接执行模式，
     已移植 A10 的 `load_executable` 逻辑到 `linker_main.cpp`：
     `AT_ENTRY == _start` 时自行加载目标 ELF，并调整 argc/argv；
   - 产物：`runtime/build/bin/source-linker64`，785,576 字节，
     sha256 `541a651a95bc26ac237367ce249cd0392b5cdf23e3a66e9f62c1bf3b2dbed1f2`。
3. **共享 libc / libdl / libm（已完成）**
   - 715 个 ARM64 源由 A9 `bionic/libc/Android.bp` 解析得到；
   - 版本脚本使用 A9 自带的 `genversion-scripts.py`；
   - `libc.so` 1,185,048 B，sha256
     `169756c2116ef4511fffb5cf6652788c265db2e234fa907f66aace74c2d113f0`；
   - `libdl.so` 12,736 B，sha256
     `f8f7d12e0167fad3f2f9767a0d3d5229fb762cba66269a16442c916a4ba5b388`；
     导出 `android_set_application_target_sdk_version@@LIBC_PLATFORM`；
   - `libm.so` 264,976 B，sha256
     `c381d4e1e07ce040f6d3745c35a732c8b7f07876eaa27b9ddbe0d89da1c60fef`；
   - 三个库都以 `--no-undefined` 链接通过。
4. **companion（结论：不重建）**
   - AOSP 9 的 arm64 把 libc++ + libc++abi + LLVM unwinder 打进
     ROM 的单个 `libc++.so`（自身定义 `_Unwind_*`，NEEDED 只有
     `libc.so`/`libm.so`/`libdl.so`）；
   - 所以直接复用 A9 rootfs 的 `libc++.so`，不新建 companion 构建。

阶段 1 退出条件（已全部通过）：

- [x] `ld-android.so` 链接通过（`--no-undefined`）；
- [x] `libc.so` / `libdl.so` / `libm.so` 以 API 28 源码构建；
- [x] A9 `libart.so` 的全部未定义符号（含 `@LIBC_PLATFORM`）由
      A9 自己的 libc/libdl/libm/ART 闭包解析；
- [x] 产物中不出现任何 `liba10vm-*`。

闭包证据：`runtime/libart-closure.json`（24 个库，
0 缺失文件、0 未解析强符号、0 未解析弱符号），审计脚本
`scripts/audit-closure.py`。

---

### 阶段 2 · API 28 项目桥（进行中）

需要为 A9 重建、或确认可安全复用的组件：

- 属性客户端（对应 A10 的 `liba10vm-property-client.so`）；
- 属性服务 `property-service`、`seed-properties`；
- `liba9binder.so`、`binder-broker`；
- guest devices / binder devices 预加载库；
- guest log；
- manager policy；
- 图形兼容库（`libEGL_emulation`、`libGLES*_emulation`、`gralloc.ranchu`、
  `hwcomposer.ranchu`）——必须用 API 28 重编，A10 产物不能直接复用；
- 独立 helper（`binder-broker`、`property-service`、`host-devices`、
  `desktop-gpu-host`）：作为宿主侧独立进程运行，绝不能把 A10 ABI 的库
  预加载进 A9 客体进程。

阶段 2 的实际进度（2026-10-22 04:0x）：

| 组件 | 状态 | 产物 |
| --- | --- | --- |
| guest-log | 完成 | `bridges/lib/liba9vm-guest-log.so` |
| property | 完成 | `liba9vm-property-client.so`、`bin/property-service`、`bin/seed-properties` |
| binder 传输 | 完成 | `liba9binder.so`、`liba9vm-binder-devices.so`、`bin/binder-broker` |
| devices/policy | 完成 | `liba9vm-guest-devices.so`、`liba9vm-manager-policy.so` |
| graphics codegen | 完成 | `graphics/generated/`（26 文件，emugen 在真机运行） |
| graphics 库编译 | 进行中 | 待编译 6 个 guest 库 |
| host-devices | 未开始 | 需要 A9 HIDL 头 |

图形库的 emugen 阻塞已经绕过：把它交叉编译成 arm64 二进制，
推到已连接的设备上运行，生成代码拉回工作区。详见
主工程的 `docs/android9-port/14-进展日志.md`。

阶段 3 · 重新生成 A9 prepared（2026-10-22 04:1x 已生成）

- `python scripts/prepare-a9.py` 产出
  `prepared`：975 native / 1215 data / 433 symlink；
- 清单 `guestSdk=28`、`frameworkSdk=28`，`platformLibraries` 已声明
  propertyClient / guestDevices / binderDevices / guestLog /
  managerPolicy / binderCore / halModules；
- `files.source` 只有 `aosp9`、`project-a9`、`redroid9`，
  `liba10vm-*` 逻辑名为 0；
- 删除 shim 的目标已经达成（清单里没有 `liba9vm-libdl-compat.so`、
  `liba9vm-malloc-leak-shim.so`）；
- **缺口**：图形 7 个库和 `host-devices` 还没编译，prepare 会在 stderr
  打印 `not built yet, skipped`，不会静默通过。

### 阶段 4 · 真机 ART 自检（已完成，2026-10-22 05:1x）

**验收证据**（`art.log`，真机 Android 15）：

```text
art_version=2.1.0
art_name=Dalvik
art_nonce=0d429392-401f-4c0a-afa6-f576e5710265
art_thread=1
art_file_roundtrip=1
art_directory_roundtrip=1
art_runtime_ready=1
```

链路：`ld-android.so → dalvikvm64 → io.a10vm.guest.RuntimeCheck`。
修掉的两个 linker 问题见 主工程的 `docs/android9-port/14-进展日志.md`：
`getauxval()` 在自我重定位前被调用；argv 需要在内核栈上左移。

### 阶段 5 · 框架与桌面（**完成，2026-10-22 13:0x**）

已通的进程与证据：

| 进程 | 证据 |
| --- | --- |
| ART | `art_runtime_ready=1` |
| property-service | `property_service_ready=1` |
| binder-broker | `binder_broker_ready=1` |
| servicemanager | `binder_context_ready=1` |
| hwbinder-broker | `binder_broker_ready=1` |
| hwservicemanager | `binder_context_ready=1`，不再 abort |
| keymaster | `Registration complete for android.hardware.keymaster@3.0::IKeymasterDevice/default` |
| keystore | `found ... IKeymasterDevice ... seclevel SOFTWARE` |
| graphics composer / allocator | `Registration complete for ...` |
| SurfaceFlinger | EGL + Adreno 830 + `Set power mode=2, type=0` |
| netd-listen（netd / mdns） | `netd_listener_ready=1`，按 `<code> <seq> <msg>` 应答 |
| SystemServer | `Entered the Android system server!`，注册 binder `netd` 与 audio 端点 |
| 系统服务 | `startOtherServices` 全过：NetworkManagement / Nsd / Connectivity / Notification / Audio / Stats / SystemUI … |
| `systemReady` | `MakeNetworkManagementServiceReady: 2ms`、`MakeConnectivityServiceReady: 7ms` |
| 开机完成 | `sys.boot_completed=1`、`desktop_user_unlocked=true` |
| Launcher3 首帧 | `Displayed com.android.launcher3/.Launcher: +903ms`，`desktop_home_drawn=1` |
| 输入 | `desktop_input_ready=1` |
| 整机启动 | `[006.33s] READY Android desktop, input and first frame ready` |
| 宿主画面（2026-10-22 13:0x） | 桌面干净显示、点图标进应用、guest 返回回桌面；无“桌面尚未打开”叠字 |
| 显示电源同步 | `framework-display-state.log` → `guest_display_interactive=true` |
| 首次启动 dexopt | 🟡 **根因已钉死**：`a9exec.log` 67 行全是 `source-linker64`、`dex2oat` 0 行，installer 仍 74 次 `0x001f` → dex2oat 的 exec 不经过 libc 的 `execve`（GuestExec），`skipDex2oat` 位置无效；下一步换层（liblogwrap 或 linker preload）（主工程 26 章 §9.2） |
| 第一次启动闪退 | **已修，真机确认**：`A9GateKeeper` 注册 no-op gatekeeper 端点，SystemServer 不再 NPE |
| 打开应用卡顿 | **修在正确的层**：`A9MediaPolicy` 注册 8 个 media 端点，消掉每个名字 5 秒等待（主工程 19 章） |
| 日志可观测性 | ✅ **已修并验证**：`GuestLogTail` 目录 bug 已改，`adb logcat -s A10VM_GUEST` 能看到实时行（主工程 18 章） |
| 电量同步 | 🟡 **百分比 + 充电状态数据源已落地**：`HostBatterySysfs` 新增 `usb/` supply（`online=1`、`type=USB`），`BatteryService` 连 `health@2.0/backup` 并 `Waited 0ms`；插拔跟随仍靠 healthd 单次 update（主工程 26 章 §9.1） |

本轮修掉的硬问题：A9 linker 缺 A10 的私有配置契约、`java.library.path` 必须是
客体路径、`/dev/cpuctl` 白名单、netd/mdns daemon socket 与应答协议、
AIDL `netd` 端点、audio 三端点桩、`/cache` 等 init 目录、Provision 抢占 HOME，
以及 13:0x 修掉的四个（见下）。

13:0x 修复（**“画面不刷新”的真相**）：

1. `gatekeeper` HIDL 服务在这个 ROM 上必然 abort（只有 `gatekeeper.redroid.so`，
   HIDL 找 `gatekeeper.default.so`），`FrameworkSession.error()` 把整个会话判死，
   桌面在 RUNNING 后 2 秒冻结在最后一帧并叠上“桌面尚未打开”。→ 不再启动它；
2. `GuestDisplayState` 未编进 A9 dex → 加入 `prepare-a9.py`；
3. `IPowerManager.wakeUp` 的 API 28 签名回退；
4. `GuestDisplayPower` 等待 5s → 25s（API 28 只能 imageless 解释执行）。

同时更正一条错误结论：宿主一直处理 `rcFBPost`
（`DesktopDecoder.cpp:238`），`EmuHWC2.cpp` 从未被修改，图形链路不是原因。

**当前剩余（按优先级）**：

1. **dexopt 探针（最先做，一次构建）**：`GuestExec.cpp` 的
   `a9_guestexec_enter` 无条件打印已写入但**未构建**；构建后看
   `framework-installer.log` 是否出现该行，确认 installd 的 exec 是否
   经过 libc 的 `execve`（主工程 26 章 §3）；
2. **host-devices（电池跟随的正路）**：让 health@2.0 的 `backup` 实例
   跑起来（A10 的 `liba10vm-host-devices.so` patch `default`→`backup`，
   或补 HIDL 头重编 A9 版），验收 `HealthServiceWrapper` 连 backup、
   插拔 10 秒内跟随（主工程 26 章 §2.4）；
3. **卡顿**：等 dexopt 清零后再看 `Slow delivery`，不要先动 imageless；
4. **阶段 11 Wi-Fi**：`GuestWifi` 未启动，镜像无 `wpa_supplicant`
   （主工程 21 章）；
5. **阶段 12 音频**：A9 的 `audioserver` 是 ELF32、profile `audio=false`；
   需要 64 位 audioserver，独立阶段（主工程 22 章）；
6. **阶段 13 相机/传感器**：`cameraserver` 32 位，host-devices 降级
   （主工程 23 章）；
7. **阶段 6 与 A10 回归**：共用层改了 `GuestLogTail` /
   `HostBatterySysfs`，A10 也要重跑。

详见 主工程的 `docs/android9-port/15-当前进度与卡点.md` 与
`docs/android9-port/17-阶段7-dexopt与电池同步.md`。

### 阶段 7 · 首次启动与电量（真机部分完成、部分待验证）

- **第一次启动闪退：已修，真机确认。**
  真机 `framework-server.log` 的崩溃栈是
  `LockSettingsService$DeviceProvisionedObserver.reportDeviceSetupComplete()`
  调用 null 的 `IGateKeeperService`。`bootstrap/.../A9GateKeeper.java` 在
  `A9Boot` 里、SystemServer 前注册同名 no-op binder 端点后，新建 A9
  第一次启动可直接进入桌面，不再闪退。
- **dexopt：真正原因是 shim 漏拦 `execvp`（已改，待构建）。**
  installd 的 dynsym 只有 `android_fork_execvp_ext` / `execl` / `execv`，
  没有 `execvp`；liblogwrap 的 `android_fork_execvp_ext()` 在 fork 后走
  `execvp`。之前 `dexopt-shim.c` 只拦 `execv`/`execve`，所以
  `A10VM_SKIP_DEX2OAT` 从未命中，74 次 `failed with 0x001f` 每次冷启动重放。
  已补 `execvp` / `execvpe`（主工程 25 章）。
- **电量：先补 `type=Battery`，跟随宿主仍待 host-devices。**
  `HostBatterySysfs` 之前写了 capacity/status/present/uevent，但漏了
  `type`；AOSP 9 `BatteryMonitor::init()` 靠 `type` 判定电池，缺了会跳过
  整个 supply。已补 `type=Battery`。healthd 仍只在 uevent 时 update，
  要跟随宿主必须编 A9 版 `host-devices`（health@2.0 `backup`），
  它卡在 HIDL 头（主工程 25 章）。

### 阶段 6 · 双运行时回归（进行中）

- 多实例管理已接入宿主：每台虚拟机独立 `files/devices/<时间戳>/`、
  独立 `VmService` 队列与原生窗口；旧的两台数据按原目录登记为既有虚拟机；
- 已完成：单台 A9/A10 启动、停止、再启动；
- 待补：A9 + A10 同时启动、同一 ROM 两台同时启动的实机记录
  （见主工程 `docs/MULTI_VM.md` 与 `docs/android9-port/11-阶段6-打包与回归.md`）。

---

## 2. 已经确认的根因（回滚依据）

- A9 `libart.so` 的未定义符号：
  `android_set_application_target_sdk_version@LIBC_PLATFORM`；
- A9 自带 `libdl.so` 导出该符号（版本 `LIBC_PLATFORM`）；
- A10 源码 libdl 只导出 `android_get_application_target_sdk_version@@LIBC_N`，
  **没有 set 版本**；
- 因此“A9 ROM 的 ART + A10 的 libdl/libc/linker”必然在 ART 装载阶段失败；
  加单符号 shim 只是把失败推迟到下一个符号或内部布局。

结论：A9 客体进程的 `ld-android.so`、`libc.so`、`libdl.so`、`libm.so`
必须全部来自 API 28 域；本阶段就是在补齐这一组。

---

## 3. 命令与产物索引

```bash
# 固定源码
sh runtime/fetch-a9-sources.sh

# 静态 Bionic + 路径/属性补丁
sh runtime/build-static.sh

# API 28 私有 linker（直接执行模式）
sh runtime/build-linker.sh

# API 28 共享运行时
sh runtime/build-libc.sh
sh runtime/build-libm.sh

# 闭包审计（阶段 1 退出条件）
python3 scripts/audit-closure.py \
  --root rootfs/system/lib64/libart.so \
  --search runtime/build/lib \
  --search runtime/build/bin \
  --search rootfs/system/lib64 \
  --search rootfs/vendor/lib64 \
  --output runtime/libart-closure.json

# 产物
runtime/build/lib/liba9vm-bootstrap.a
runtime/build/bin/source-linker64
runtime/build/lib/libc.so
runtime/build/lib/libdl.so
runtime/build/lib/libm.so
```

输入源码全部固定为 `android-9.0.0_r61`；外部仓库 `jemalloc` 固定提交
`f1df06c9b25685e61e991e254dfd9b91a9b58d5b`。

---

## 4. 明确不再采用的做法

- 不再把 A10 的 linker/libc/libdl/libm/libc++/libunwind 放进 A9；
- 不再用单个符号 shim “修” ABI 缺失；
- 不再把 helper 进程的 A10 库预加载进 A9 客体；
- 不在 ART 自检通过前启动系统服务；
- 不再用全局符号并集审计代替按进程实际加载闭包的检查。
