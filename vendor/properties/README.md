# P2：独立私有属性组件与早期 API 验证

2026-10-07，普通 APK UID 下通过。复用 G1 的 Android 10 linker/Bionic，
在独立组件内运行真实 AOSP 属性区与 property-info parser。私有属性返回 SDK 29，
宿主对照与客体 libc 的 `__system_property_get()` 仍返回宿主 SDK 36。
新增真实 AOSP API archive 的独立可执行文件，验证静态初始化前读到 SDK 29、
构造器之后状态保持，以及读写端与失败重试。此文件的 API 来自自身链接的 archive，
尚未接入 linker 静态 libc 和动态客体 libc。
**运行时接入、完整 G1/P2 与 Android 桌面仍待完成。**

## 构建与复现

在仓库根目录执行：

```sh
sh container-probe/properties/prepare-apk.sh \
  artifacts/guest/android10-display-v4.raw \
  artifacts/container/gate-0-repeatable/report.json
python3 container-probe/properties/test-native.py
sh tools/build-termux.sh -PcontainerProbe -PcontainerProperties \
  :container-probe:assembleDebug :container-probe:lintDebug --offline
sh container-probe/test-device.sh --local-root --properties
```

ADB 开发设备使用最后一条命令去掉 `--local-root` 的入口。Root 仅用于开发安装、
Instrumentation 调度和报告回收；写端与 `fork + exec` 后的独立读端均保持普通
APK UID、零有效 capabilities 和 `untrusted_app` 域。脚本结束卸载本轮测试包。
归档 APK 为 `artifacts/container/properties/probe.apk`，可传为脚本最后一个参数。

`fetch-sources.sh` 按 [sources.txt](sources.txt) 获取 28 个固定输入并核对
[sources.sha256](sources.sha256)。Bionic 固定为
`290c0cb5044b643e5d6cbcb1a5b275541ca3a89e`，system/core 固定为
`1dea9a052b7f214c10a77d5ed6ffd3602722a817`，均来自 `android-10.0.0_r47`。
缓存位于 `artifacts/container/properties/upstream/`；补丁只应用于 `derived/`。
编译器为 Termux clang，NDK r29 只提供公共头、链接 stub 与静态 C++ 库；
不链接其静态 libc，不将它标作 Android 10 私有运行时。

## 实现边界

- `PropertyInfo` 生成一个默认 context/type 的有效 property-info；尚未实现完整
  Android 产品属性上下文表、property-service socket 或持久化属性服务。
- `PrivateProperties` 包装单写端、只读端和已有 `ro.*` 的重写限制，保留 AOSP
  属性布局、serial、共享 futex 与长只读属性 callback。
- [platform.patch](platform.patch) 把 `ContextsSerialized` 的路径改为实例目录，
  文件属主检查改为当前 euid/egid；文件保留应用自然 SELinux 标签。
  只接受序列化上下文，不回退宿主 `/dev/__properties__` 或旧 split 格式。
- 通过显式 `SystemProperties::Reset()` 释放映射；包装组件析构时调用它，
  静态 API 对象则保留 AOSP 要求的平凡默认构造/析构，避免覆盖早期状态。
  初始化失败时立即释放，修正未初始化 `Wait()` 的 bool 返回值。
- 已初始化的读端只接受原目录的重复初始化；切换实例返回失败，原实例继续可读。
- 使用同版 AOSP `async_safe_log.cpp`，移除 stdio 适配。
- [runtime.patch](runtime.patch) 在真实 `system_property_api.cpp` 中接入绝对路径
  `A10VM_PROPERTIES`；读端和写端均拒绝缺路径与相对路径，不回退宿主属性区。

`build-library.sh` 编译 8 个对象，生成 `lib/liba10vm-property-core.a` 和
`lib/liba10vm-property-runtime.a`。前者排除 API 对象，保留 libc 的宿主属性对照；
后者包含真实 API，供下一阶段两套 libc 使用。API 对象无 `.init_array/.fini_array`，
runtime archive 没有 stdio、malloc/calloc/realloc 未定义依赖；仍有 syscall、
mmap、getenv、C++ delete 等依赖，完整自举闭包尚待构建。

`EarlyInit` 从 linker 传入的环境建立 `environ`，再在 `.preinit_array` 调用 API，
遵循 AOSP `__libc_init_common()` 中“先 environ、后属性”的顺序。该测试发生在
可执行文件构造器之前，运行时已有 linker/TLS；不等同于整个 libc 自举已完成。
`--wrap=malloc/calloc/realloc` 统计静态输入中的直接调用，不覆盖共享 libc 内部调用。

没有使用 `LD_PRELOAD`、SDK 返回值硬编码或 ELF 字符串替换来宣称运行时接入。

## 已验证结果

本机宿主 SDK 36、target SDK 35、4 KB 页；受测 APK UID 为 `10743`。
本地真实目录与路径别名场景各通过 46 个独立检查；设备客体与宿主对照也各通过
相同 46 项。重复打开检查实际执行 20 次，统计数字按独立检查名去重。
新增 API 在本地真实路径、别名路径以及设备客体/宿主对照中各通过 5 个场景。

| 验收项 | 结果 |
| --- | --- |
| 私有 SDK / 宿主 SDK | `29 / 36`；实例之间互不串读 |
| 独立读进程 | 同 UID、不同 PID，重新 exec 装载；3 个 `r--s`，0 个 `rw-s` 映射 |
| 写端 | 2 个 `rw-s` 映射 |
| property / global serial | `117440512 → 117440514` / `4 → 5` |
| property / global wait | `40116 / 30243 µs`；写端刻意延迟 30 ms，覆盖阻塞等待 |
| 边界与生命周期 | 等待超时、400 字节只读 callback、长可写属性拒绝、打开/释放与失败释放通过 |
| 文件检查 | 可写 property-info、属性区符号链接拒绝；测试实例已清理 |
| 库闭包 | 客体 executable maps 仅含已核对的 APK 原生文件；宿主对照被识别 |
| 早期 API 读端 | 初始化结果 0、SDK 29、global serial 5，进入 main 后状态保持；3 个只读共享映射 |
| API 负测与重试 | 缺路径、相对路径、不完整目录初始化失败，未初始化读取为空、wait 返回 false；随后恢复成功 |
| API 写端 | 创建、增加/更新、property/global serial 与 2 个共享可写映射通过 |
| 工件检查 | APK ZIP、v2 签名、7 个原生输入与报告哈希通过；Lint 0 errors / 8 warnings |

设备最初的映射判定按目录字符串匹配，因 `/data/user/0/...` 与内核 maps 的
`/data/data/...` 别名差异失败。现按设备号、inode 和映射权限判定；本地回归使用
临时符号链接复现名称差异，仍要求读端只读、写端共享可写。
红报告保留为 `device/mapping-red-report.json` 与 `device/mapping-alias-red-report.json`。
本轮还保留 `trivial-constructor-red.log` 和 `api-instance-binding-red.log`：前者证明
析构/成员初始化破坏 AOSP 构造约束，后者复现切换目录被错误接受。最初的早期测试
未接入 `environ`，失败记录为 `api-initial-red.log`；修正测试顺序后通过。

原始报告为 `artifacts/container/properties/device/report.json`，归档构建/Lint 和
本地日志位于同一 properties 目录；精简证据见
[container-properties-p2.json](../../docs/evidence/container-properties-p2.json)。
Lint 的 8 项提示沿用测试包的 target API、固定方向、ChromeOS ABI、备份、图标及
Surface 测试页静态引用/可访问性检查；本轮没有为属性组件扩展窗口 UI。

## 下一步

按 [源码构建计划](../../docs/CONTAINER_BUILD.md) 继续同版 Bionic/linker 自举闭包，
把实例属性区接入两套 libc 初始化并验证线程/TLS/装卸与缺库失败；随后实现
Binder/HwBinder 语义，最后推进系统合成与桌面。SDK 29 目前证明独立组件及
executable 内的 API archive，动态 libc 的 API 对照仍为 SDK 36。

KISS/YAGNI：只实现当前属性切口；不增加整盘副本或通用后端框架。
SRP/SOLID：property-info、属性访问、进程协作和 APK 判定分别负责。
DRY：复用现有库提取、进程管理与身份/映射判定；路径别名回归复用同一原生检查。
