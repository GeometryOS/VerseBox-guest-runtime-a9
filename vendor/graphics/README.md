# 两仪式图形路线：Android 10 编码端与宿主窗口

## 当前主应用与多指输入（2026-10-08）

共享 `:native-display` 已成为主应用 `0.2.0-native-display` 的唯一显示入口。
图形协议仍使用配套 Android 10 GL2Encoder/decoder；输入升级为独立 `touch-v2`：
192 字节、最多十指，传递 pointer ID、动作索引、坐标、pressure、时间和序号，
转发历史 MOVE。宿主发送线程与客体输入线程独立于 GL；512 条有界队列保持事件顺序。
STOP 排在输入队尾，退出前客体校验已经应用的输入序号。

当前普通 Termux UID 的生产链路通过正常绘制、图形传输暂停、立即停止三项回归。
正常与暂停场景各完成 50 帧/205 事件；立即停止由修复前 2/205 条恢复到 205/205 条。
构建后的主 APK/图形探针输入摘要均核对通过；新版 Surface 尚待设备实测。
60 Hz 为目标节拍，**完整 Android 桌面仍待接入**。见
[当前证据](../../docs/evidence/native-display.json)。

```sh
sh tools/prepare-native-display.sh
env -u LD_LIBRARY_PATH -u LD_PRELOAD python3 tools/test-native-display.py
env -u LD_LIBRARY_PATH -u LD_PRELOAD python3 tools/test-native-display.py --stop-pending
env -u LD_LIBRARY_PATH -u LD_PRELOAD python3 tools/test-native-display.py --stall-input
```

## 历史 P1a/P1b 记录

以下 30 Hz、单点输入和旧 APK 的设备结果保留原始测试口径。

这是 [两仪方案](../../docs/TWOYI_ANDROID10_PLAN.md) 的 **P1a 离屏与 P1b 窗口验证**。
沿用原有 G1 探针，在普通 APK UID 下验证 Android 10 编码客户端到宿主 GPU 的路径：

```text
Android 10 linker / Bionic + Android 10 GL2Encoder
    -> 私有 Unix socket：配套 GLES 命令及返回值
    -> 同一定义生成的 decoder
    -> 宿主 EGL / GLES -> GPU pbuffer / SurfaceView
触摸 -> 独立控制通道 -> 客体输入序号 -> 绘制反馈 -> PixelCopy 核对
```

没有使用两仪的原生二进制，没有启动 QEMU，没有下载新 ROM。Root 仅用于开发手机
上的安装、Instrumentation 调度和报告提取；两个受测进程都由 APK 自身启动。

## 历史结果：P1b（2026-10-07）

完整报告位于 `artifacts/container/graphics/window/device/report.json`，可提交的
精简证据见 [P1b 清单](../../docs/evidence/container-graphics-p1b.json)。

| 检查 | 结果及范围 |
| --- | --- |
| 运行身份 | 宿主和客体均为普通 APK UID 10739、`CapEff=0`、`untrusted_app`；客体库闭包检查通过 |
| GPU | `EGL 1.5 / Qualcomm / Adreno (TM) 830`，真实窗口与带序号的像素通过核对 |
| 稳态窗口 | 360×640、目标 30 Hz，实测 60,008.63 ms；1,800 帧、1,799 个帧间隔 |
| 帧间隔 | swap 返回间隔 P95 **34.83 ms**，满足本探针 ≤ 50 ms 的预算 |
| 输入反馈 | 100 个输入样本，注入到 Surface 像素采样 P95 **76.26 ms**，满足 ≤ 150 ms 的预算 |
| 窗口重建 | 10 次 Activity/Surface 重建，guest PID 始终为 32051；合成截图恢复需 93.29–182.21 ms |
| 正常停止 | 停止命令写入成功，客体确认收到停止并完成绘制；无强制终止，退出码 0，退出身份复核通过 |
| 回归 | 既有 G1 和 P1a 设备检查通过；8 项原生协议测试通过；探针构建/Lint 为 0 errors / 8 warnings |
| APK | `p1b-window` / versionCode 3，2,077,372 字节，ZIP 完整性、v2 签名、8 个原生 ELF 与输入清单一致；测试后卸载成功 |

帧间隔取自 `eglSwapBuffers` 返回，输入指标包含控制传输、绘制和 `PixelCopy`
采样。重建恢复另用 compositor screenshot 复核；上述指标没有测量物理屏幕的光学
延迟。首次 Surface 采样为 740.27 ms，计时对象是这个小探针的启动。
Android 10 桌面的启动、应用点击和系统稳定性将在 P3 单独验收。

## 历史结果：P1a 离屏（2026-10-06）

本机宿主 SDK 36、4 KB 页；APK minSdk 30 / targetSdk 35。设备报告位于
`artifacts/container/graphics/device/report.json`，精简证据见
[`docs/evidence/container-graphics-p1a.json`](../../docs/evidence/container-graphics-p1a.json)。

| 检查 | 结果及含义 |
| --- | --- |
| 普通应用身份 | 客体和渲染进程 UID 与 APK 一致；`CapEff=0`，SELinux 为 `untrusted_app` |
| 客体库闭包 | 5 个文件型可执行映射仅包含编码程序及核对过的 Android 10 linker/libc/libm/libdl；未混入宿主 ELF |
| 真实 GPU 调用 | EGL 1.5，`Qualcomm / Adreno (TM) 830`；宿主 GL 字符串来自实际当前 EGL context |
| 像素往返 | 64×64 pbuffer，32 轮颜色/裁剪绘制，64 次 RGBA8 像素校验；共 359 个生成协议命令 |
| 本地协议负测 | 指纹不匹配、checksum 序号损坏、截断、错误长度、未知命令和未支持的 pixel-store 布局均检出 |
| 既有 G1 | 正测、宿主对照、缺 libc、身份判定负测保留并通过 |
| APK | 当时约 1.8 MiB，ZIP 完整性、v2 签名与安装文件哈希已检查；实测后卸载探针 |

`elapsed_ms` 是 **32 轮离屏命令、GPU 绘制及像素回读**的合计时间；Java 的
`guest.elapsedMs` / `host.elapsedMs` 还包含进程和测试调度。都不是 Android 开机
时间、窗口帧率或触摸到物理屏幕的延迟。该原型没有替换 `io.a10vm` 后端。

P1a 的历史 APK 哈希保留在旧清单中；唯一图形候选 `artifacts/container/graphics/probe.apk`
现已更新为上面的 P1b APK。

**仍未完成：**renderControl、完整客体 EGL、gralloc/HWC/SurfaceFlinger、私有 Binder/HwBinder
和 Android 10 桌面。客体属性读取仍返回宿主 SDK 36，私有属性也未通过完整 G1。
当前小探针仅有两个子进程，不代表 Android 全系统的进程存活门槛已经通过。

## 固定源码与适配边界

| 来源 | 固定提交与用途 |
| --- | --- |
| AOSP `platform/external/qemu` | `9e4088bf54f2a11dd7898b757c08e2c473315460`：emugen、GLES 定义、生成的 decoder、checksum 算法 |
| AOSP `device/generic/goldfish-opengl` | `d291bbd969a8cdb7bf9ac6f7d46af5b3d2b6c309`，Android 10：`GL2Encoder`、状态跟踪、校验、纹理及索引辅助实现 |
| 既有审计镜像 | 与 G1 共用同一锁定镜像及提取清单；仅提取 linker/libc/libm/libdl，无新增整盘副本 |

`sources.txt` 限定下载范围，`sources.sha256` 固定缓存文件内容；原件保留上游版权
与许可证。生成器和源码双端均参与构建，没有用空 helper 满足链接。生成的
`gl2_*` 位于 `artifacts/container/graphics/generated/`，不是直接混用两仪旧 codec。

平台差异集中在 `platform.patch` 与 `compat/`：

- 去掉本原型不用的桌面快照接口；checksum v1 的计算和计数规则保留。它检查长度
  与序号，**不是内容 CRC 或认证机制**。
- `ChecksumCalculatorThreadInfo` 使用真实 `thread_local`；日志写 stderr，异常
  终止当前连接并使测试失败。
- `IOStream` 使用真实缓冲和 socket 收发，处理分片、EOF、断线和超时。
- 不宣告 Goldfish DMA 能力；DMA 调用明确报错。未加入硬件设备路径或成功空实现。
- GLES 2 初始化先查询宿主顶点属性上限，再初始化客体 FBO 状态、执行 make-current
  状态更新；没有跳过 AOSP 客户端的校验和状态跟踪。

`ProbeDecoder` 只分派当前验证用到的 GLES 子集，其他命令先报错。`PixelReadback`
只接受单个紧密排列的 RGBA8 像素；未实现的 pixel-store 布局不转给驱动。
这不是一个已完成的通用 GLES 驱动，也没有向客体应用导出完整 `libEGL.so`。

## 重现

在仓库根目录运行，复用已有 Termux clang、NDK sysroot 和 Android 构建缓存：

```sh
# 仅原生命令往返测试；这一层使用 Termux/宿主 Bionic，不冒充客体库隔离结果。
sh container-probe/graphics/build-native.sh
python3 container-probe/graphics/test-native.py

# 真正 Android 10 库 + 普通 APK UID：复用 G1 准备入口，加入编码器和两个渲染入口。
sh container-probe/graphics/prepare-apk.sh \
  artifacts/guest/android10-display-v4.raw \
  artifacts/container/gate-0-repeatable/report.json
sh tools/build-termux.sh -PcontainerProbe -PcontainerGraphics \
  :container-probe:assembleDebug :container-probe:lintDebug --offline
sh container-probe/test-device.sh --local-root --graphics

# P1b 完整窗口、100 次输入和 10 次 Activity/Surface 重建。
A10VM_WINDOW_MS=60000 A10VM_WINDOW_TOUCHES=100 \
  sh container-probe/test-device.sh --local-root --surface

# 或使用已经连接的 ADB；运行中的探针本身同样不调用 Root/ADB。
sh container-probe/test-device.sh --graphics
sh container-probe/test-device.sh --surface
```

每次选择一种安装通道与测试模式，末尾可附加 `artifacts/container/graphics/probe.apk`。
`--graphics` 运行离屏测试，`--surface` 同时运行 G1、P1a 和 P1b；窗口报告写入
`artifacts/container/graphics/window/device/`。脚本保留已有同名安装，只清理本次安装
的测试包。默认窗口参数即 60 秒和 100 次输入；缩短参数只适合调试，不计作预算验收。

本地安装清理回归：`python3 container-probe/test-cleanup.py`。构建脚本的
`A10VM_GL_DEBUG=1` 可保留当前原生程序的调试信息，不额外复制另一组二进制。

## 本轮实际修复

1. **构建依赖收敛。** 原桌面 `IOStream` 引入了 emulator 的 Stream/Optional/Log
   闭包；改为小型平台 I/O 适配，只保留本次真实通信所需接口。
2. **静态 libc 误链接。** Termux clang 配合额外的 NDK 库搜索目录曾选中 `libc.a`，
   程序启动崩溃。现在显式链接 C++ 静态库，Bionic 仍使用 API 29 动态链接 stub，
   构建后核对 `DT_NEEDED` 并去除构建机 RUNPATH；运行库来源另由设备映射验证。
3. **FBO 状态初始化。** 调试栈定位到 `GLClientState::getBoundFramebufferFormat`；
   补齐 `initFromCaps` 与 make-current 顺序后，真实像素往返由失败转为通过。
4. **原型边界。** 固定 1 像素回读不支持改变 pack 布局；先以该命令负测复现旧分派
   仍会接受它，再移除未实现的入口，回归通过。
5. **客体退出 137。** 移除图形客户端的 `PR_SET_PDEATHSIG`，保留启动时父 PID
   一致性检查，使用独立控制通道的停止命令收拢客体。成功判据同时要求停止写入、
   停止/完成/退出日志、退出码 0 和退出身份检查；强制结束的运行判为失败。

## 模块、存储与下一步

- **SRP：**传输、协议适配、编码客户端、GPU 解码端、进程身份和 Android 测试调度
  分文件。窗口 EGL、渲染、JNI 和 Java 会话各自独立；Activity 只负责窗口与触摸。
- **DRY：**复用 G1 的库校验、运行时目录和进程环境；两端共用同一生成协议和指纹。
- **KISS / YAGNI：**只做可检验的 GLES 2 小闭环，没有增加多实例、Vulkan、ROM 下载器
  或新的通用后端框架。
- 归档只保留一份图形候选 APK：`artifacts/container/graphics/probe.apk`。固定源码
  缓存、生成协议、最终构建与测试证据保留；可重建的 Gradle 输出在归档后清理。
- P1b 通过后进入 **P2：私有属性、同版运行时与 Binder/HwBinder**，按原方案逐关
  验证，再推进 P3 的系统合成和完整桌面。启动和点击延迟以 Android 10 桌面验收。

工具简报：本地 Shell、Clang、GDB、Gradle、APK 签名工具；设备测试由 Root 调度，
被测进程身份来自运行时记录。未调用外部 MCP。
