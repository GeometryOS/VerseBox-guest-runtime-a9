# guest-runtime-a9/rom —— Android 9 的 ROM 工具链

这个目录属于 **A9 运行时仓库**，自带出 ROM 包所需的全部东西。
A9 / A10 / A15 是三个独立仓库（各自 clone、各自 CI、各自发布），
所以**不引用别的仓库的脚本**：只 clone `guest-runtime-a9`，就能出 A9 的 ROM 包与 APK payload。

| 文件 | 作用 |
| --- | --- |
| `pack-rom.py` | 打包器：`prepared/` + 自述 → `dist/versebox-<id>-<version>.zip`（新格式） |
| `rom_hash.py` | `payloadSha256` 的唯一实现（算法是**合同**，见下） |
| `stage-apk-payload.py` | 旧格式（打进 APK）：客体 .so → `assets/guest-runtime-a9/lib/**`，`jniLibs` 只留 resident |
| `rom.properties.yaml` | ROM 身份（`id` / 名字 / `defaultRomVersion` / `libMode`） |
| `resident-native.txt` | 必须留在 APK `lib/arm64-v8a` 的逻辑名（被 `execve` 的那几个） |

## 用法（在 guest-runtime-a9 根目录）

```sh
# 新格式：ROM 包
python rom/pack-rom.py --prepared prepared
python rom/pack-rom.py --verify-only dist/versebox-android9-1.0.0.zip

# 旧格式：APK 侧 payload（Gradle 消费准备好的那一份，不是 prepared/）
python rom/stage-apk-payload.py --prepared prepared --out prepared-apk
```

`scripts/build.sh` 已经把这两步接进构建链：`--skip-rom` 只跳过 ROM 包，
`--rom-version` / `--rom-out` / `--lib-mode` 原样透传给打包器。

## 为什么这里有一份「和 A15 一样」的脚本（这不是抄，是边界）

`docs/rom-package/11` §1：三个运行时是三个仓库。脚本**不能**跨仓库引用——
那会让"只 clone A9"的人打不出 A9 的 ROM，也会把两个域的发布节奏焊在一起。

三个仓库之间唯一必须**逐字节一致**的东西是 `rom_hash.py` 的规范化算法
（`docs/rom-package/02` §6.2，app 侧解包器的 L2 校验只认这一种）。核对方法：

```sh
python rom/rom_hash.py <同一个 payload 目录> --json   # 三个域必须同值
```

`pack-rom.py` 的**契约**（zip 根三项、`data[].name` 只改前缀、`files[].libPath`、
包内自检清单）同样由 `docs/rom-package/02`–`05` 规定；本仓库只允许在
「域默认值」上不同（id、清单名、前缀、resident 清单、dist 目录）。

## 判据

```text
prepared-apk/jniLibs/arm64-v8a/*.so      = resident 清单（A9 是 2 个）
prepared-apk/assets/guest-runtime-a9/lib/ = 其余 983 个
dist/versebox-android9-1.0.0.zip          --verify-only 通过；libMode=rom；bundledLibs=985
```

## 版本号

`defaultRomVersion` 由维护者决定，默认 `1.0.0`；不要为了"区分这次构建"改它
（`_audit/00` §4.7）。区分构建看产物 `sha256` 与字节数。