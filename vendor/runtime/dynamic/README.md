# P2：源码 linker 与动态 Bionic

2026-10-07，同版 Android 10 linker、动态 `libc.so` 和 `libdl.so` 已从源码构建，
本地及普通 APK UID 的四种属性场景、四种装载失败场景全部通过。每个运行场景
覆盖 23 个独立检查，包括共享 globals、pthread/ELF TLS、并发分配、动态 TLS、
线程析构与 DSO 卸载。文件型可执行映射只包含本次核验的程序及配套运行库。
精简记录见[动态运行库证据](../../../docs/evidence/container-runtime-dynamic.json)。

## 构建与复现

在仓库根目录执行：

```sh
sh container-probe/runtime/dynamic/prepare-apk.sh \
  artifacts/guest/android10-display-v4.raw \
  artifacts/container/gate-0-repeatable/report.json
python3 container-probe/runtime/dynamic/test-native.py
sh tools/build-termux.sh -PcontainerProbe -PcontainerDynamic \
  :container-probe:assembleDebug :container-probe:lintDebug --offline
sh container-probe/test-device.sh --dynamic
```

最后一条通过 ADB 安装、调度 Instrumentation 和回收报告，探针由 APK 直接启动。
此前设备验收使用 `--local-root` 进行开发安装与调度，受测 UID 为 10769、
capabilities 为零、SELinux 域为 `untrusted_app`。测试结束卸载临时包，保留已有
同名安装；安装清理的 7 项回归通过。`--dynamic` 同时运行 G1 装载、独立属性、
属性 API 和静态自举检查。

单独构建运行库可依次执行 `build-linker.sh`、`build-libc.sh`、`build-probes.sh`。
源码基线更新时使用 `--record` 生成编译依赖清单；正常构建校验内容。对象缓存
核对编译命令、编译器身份、全部 `.d` 输入摘要与对象摘要，继续复用已有成果。

## 源码与模块边界

基线为 `android-10.0.0_r47`。复用[静态自举模块](../README.md)的 Bionic、
system/core、libc++、libc++abi 固定提交和属性适配；新增：

| 仓库 | 固定提交 |
| --- | --- |
| external/zlib | `73968beb892091e38d611e4ca559b8580226c86b` |
| external/jemalloc_new | `8129245231d25fdcd829e00fda895d177d552cdf` |
| build | `475fb5e5aa709812a799c2c411568b3cd12bf50e` |

- `build-linker.sh`：22 个 linker 源和 150 个配套源，复用真实静态 Bionic archive。
  产物没有解释器、DT_NEEDED 或未解析符号，SONAME 为 `ld-android.so`。
- `select-libc-sources.py` / `build-libc.sh`：根据同版 `Android.bp` 选择 718 个
  ARM64 shared-libc 源，使用真实 jemalloc；生成 libc/libdl 的版本脚本并直接
  调用 `ld.lld`，避免编译器驱动注入宿主库。libc 只依赖配套 libdl 和 linker。
- `compile-platform.sh` / `compile-source.py`：平台头、编译参数与增量缓存。
- `build-probes.sh` / `test/`：真实 CRT、线程、分配及动态装卸行为。
- Java `DynamicRuntimeProbe`：私有配置、装载正负测与报告；与静态自举共用
  `RuntimeMappings`，按实际文件、映射权限、偏移和副本数校验属性区。

[sources.sha256](sources.sha256) 有 607 条输入记录，
[libc-sources.sha256](libc-sources.sha256) 有 1233 条；补丁单独计入组件摘要。
Gitiles archive 的时间戳不稳定，因此固定解包后的文件内容。

## 已验证行为

| 场景 | 属性与装载结果 |
| --- | --- |
| `reader` | executable preinit 的 SDK 为空；constructor/main 为 29；父进程和 `fork`/`execve` 子进程各有三种属性文件的两份 `r--s`、偏移 0 映射，共 12 条 |
| `missing` / `relative` / `incomplete` | SDK 为空；无残留属性映射；运行库和线程正常退出 |
| 缺配置 / 相对配置 / 无匹配配置 | 装载器在程序 main 前终止，设备退出码为 134 |
| 缺 libc | 报告缺库并以 1 退出，实际没有补入宿主 libc |

动态程序的 AOSP 顺序是 executable preinit → 依赖库 constructors → executable
constructors。动态 libc 的属性初始化位于其 constructor，故 preinit 的空值是
本次实际行为；静态自举仍在 preinit 前完成属性初始化。测试保留这一区别。

每个场景还核验：

- linker/libc 共用 `libc_shared_globals`，TLS generation 同步，`dladdr` 返回两者
  不同的实际映射及安装路径。
- 父进程 `fork()` 后通过源码 linker `execve()` 启动子进程；子进程重新核验属性、
  linker/libc 映射与 APK UID 身份，父进程检查 `waitpid()` 的零退出状态。该项验证
  显式 linker 子进程链，不代表通用 Android `exec`/APEX 接入。
- 两轮 pthread、独立 errno/ELF TLS、pthread key 与 C++ thread-local 析构。
- 4 个工作线程同时执行分配，每线程 64 轮 malloc/realloc/calloc/posix_memalign。
- 线程先创建、DSO 后装载，验证该线程的动态 TLS。`dlclose` 后 TLS 析构引用
  保留 DSO，线程退出后真实卸载；连续两轮都通过。
- fini 执行后，真实 Bionic 退出清理刷新 stdio 缓冲。

同轮设备测试的 G1 正负测、46 项独立属性检查、5 个属性 API 场景及 4 个静态
自举场景也通过。动态运行时设备 UID 为 10769，capabilities 为零；设备为 SDK 36 /
ARM64 / 4 KB 页，APK target SDK 35 / min SDK 30；
其他宿主版本与 16 KB 页设备待覆盖。小程序调用耗时不作为完整桌面启动或点击指标。

归档 APK 为 `artifacts/container/runtime/dynamic/probe.apk`，约 2.54 MiB，13 个
原生输入。ZIP 完整性、v2 签名、打包输入与设备报告哈希一致；Lint 为 0 errors /
8 warnings。linker、libc、libdl、探针与 TLS 模块重复构建的哈希均一致。
原始报告在 `artifacts/container/runtime/dynamic/device/report.json`。

## 问题修正与后续

`get_libdl_info()` 的合成 linker 对象现在复制真实 base/size，使 `dladdr` 能定位
源码 linker；直接启动模式通过 `/proc/self/exe` 保存真实私有路径，debug link-map
复用持久的 realpath。配置仅取 `A10VM_LD_CONFIG` 指定的现存绝对路径，读取失败
或无匹配节立即终止。[platform.patch](platform.patch) 还适配 Python 3 AID 生成器。

当前不接入平台 debuggerd 信号收集，linker 的该调用由 `A10VM_DEBUGGERD` 控制；
装载失败诊断仍实际输出。本轮补齐源码 linker 下的 `fork`/`execve` 子进程关卡，
四种属性状态和四种装载失败均通过；属性 set、时区、Unicode 的配套库、通用路径/
exec/APEX 适配和虚拟身份仍待完成。当前入口仍由 Java 显式调用私有 linker，子进程
测试不等于系统服务的通用进程启动。随后推进运行时边界、Binder/HwBinder、ART、
系统合成和真实桌面。完整 G1/P2 尚待验收。

KISS / YAGNI：复用镜像和小探针验证当前 ABI。DRY / SRP：构建、输入校验、
运行与映射判定各有独立模块，共用已有源码准备和进程回收路径。

## 后续进展：配套运行库（2026-10-07）

[配套模块](../companion/README.md)已从同版源码生成 libm、libc++/libc++abi 和
适配的 AArch64 LLVM 展开器，接到本模块的 linker/libc/libdl。普通 APK UID
两轮各通过 39 项检查，跨库异常、线程/TLS 析构、动态卸载及回溯均有实际结果。
两种异常终止、三种缺库负测和本模块的四种属性状态/四种装载失败同轮通过。
私有配置已抽到共用 `RuntimeConfiguration`；属性握手回归现为 48 项。

源码差异和精简证据见[配套运行库清单](../../../docs/evidence/container-runtime-companion.json)。
上文保留为动态阶段的历史结果；接下来推进私有 Binder/HwBinder 和 ART/桌面。

## 后续进展：免 root 路径与 exec（2026-10-08）

[文件系统模块](../filesystem/README.md)把客体路径和 `execve` 适配接入源码
静态 Bionic、linker 和动态 libc；未设置客体 rootfs 环境时保持宿主调用语义。
当前库的本地四种属性状态、四种装载失败及静态自举四场景均重新通过。
新文件系统探针的 29 项检查、父子独立核验和四项判定负测通过，APK 内容校验通过。

`container-runtime-dynamic.json` 中的设备报告与 native 字段描述历史 APK 内的 ELF；
当前 linker/libc 已重建，新的输入摘要见
[文件系统证据](../../../docs/evidence/container-runtime-filesystem.json)。本轮没有 APK
设备运行结果，历史报告的 UID、耗时和输入哈希保持原测量口径。
