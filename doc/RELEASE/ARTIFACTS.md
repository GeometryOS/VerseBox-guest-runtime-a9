# 构建产物与版本

这个仓库的 release 会带上六件产物。它们全部由 `prepared/` 长出来，命名里的版本来自 `rom/rom.properties.yaml` 的 `defaultRomVersion`，与标签无关。

## 1. 带哪六件

| 文件 | 里面是什么 | 谁消费 |
| --- | --- | --- |
| `a9-runtime-<标签>.tar.gz` | prepared 树的压缩包，运行时目录原样 | 复现与比对运行时 |
| `versebox-android9-<ROM 版本>.zip` | ROM 包，宿主导入的那一份，装载荷与两份清单 | 宿主导入 |
| `versebox-android9-<ROM 版本>-min.zip` | 最小占位包，只有一个自述文件 | APK 声明支持这个系统 |
| `apk-resident-libs-android9.tar` | APK 侧必须常驻的原生文件，清单在 `rom/resident-native.txt` | APK 打包 |
| `pack-android9-<ROM 版本>.json` | 打包报告，记着载荷哈希、载荷字节、文件数、承载方式 | 追溯这个包怎么来的 |
| `SHA256SUMS` | 上面五件的哈希，一行一件，不含自身 | 下载后的校验入口 |

名字里的版本取自配置文件里的默认版本，当前是 `1.0.1`，于是 ROM 包与打包报告分别叫 `versebox-android9-1.0.1.zip` 与 `pack-android9-1.0.1.json`。

这一节不抄字节数。字节数每次构建都会变，抄进文档就等于留一行一定会过期的数字。要数字就去那一次运行的 `SHA256SUMS` 与打包报告里取，两处本来就记着。本仓库完成第一次正式发布之后，可以在这里补一行实测记录，但只许抄 `SHA256SUMS` 里的值，不许写估计值。

## 2. 它们由什么长出来

```text
prepared/
   │
   ├── tar ──▶ a9-runtime-<标签>.tar.gz
   │
   └── rom/pack-rom.py ──▶ versebox-android9-<ROM 版本>.zip
                                │
                                ├──▶ versebox-android9-<ROM 版本>-min.zip
                                ├──▶ apk-resident-libs-android9.tar
                                └──▶ pack-android9-<ROM 版本>.json

五件一起 ── sha256sum ──▶ SHA256SUMS
```

三条链路，一条汇聚。prepared 是唯一输入，ROM 包与最小占位包、常驻库、打包报告都由同一次打包派生，最后五项一起算哈希。任何一步失败，六件里就缺一件，发布工作流的第三道闸门会先拦下来。

最小占位包与常驻库由打包器在同一个进程里反推出来，不是第二次打包。这样做是为了让"能装但开不了机"的组合根本产不出来，见 `rom/apk_artifacts.py` 的说明。

## 3. 三个数字最容易看错

`payloadBytes` 比任何一个文件都大。它算的是 payload 目录里未压缩内容的总字节，压缩包自然小一半。

ROM 包与运行时 tar 包数量级相近，两者压的不是同一批东西。tar 压的是 prepared 整棵树，zip 压的是 payload 与两份清单，目录结构不同，不能拿一个的大小去推另一个。

最小占位包只有几百字节，不是坏包。它里面只有一个自述文件，作用是让宿主知道这个系统受支持，真正的客体内容在完整包里。

## 4. 两种哈希别混

zip 与 tar 的 sha256 证明下载完整。字节对得上就说明文件没在传输里坏掉，与内容是什么无关。

`payloadSha256` 证明内容身份。它只对 payload 目录里的字节算，压缩方式、时间戳、目录顺序都不影响它，算法在 `rom/rom_hash.py` 里，宿主侧的解包器认的是同一套。ROM 包里的 `versebox_properties.yaml` 记着这一个值，索引里也记着同一个值，两处相同就能证明这个 zip 就是索引里那一次构建产出的。

## 5. 客体 .so 装在哪一侧

本仓库的自述里 `libMode` 是 `rom`，也就是说 DLOPEN 加载的客体 .so 放在 ROM 的 `payload/lib/`，APK 的 `lib/arm64-v8a` 只留 `rom/resident-native.txt` 里那两个被 execve 的文件。

这条边界不是风格选择。App 进程在 `u:r:untrusted_app` 域里对 `app_data_file` 没有 `execute_no_trans`，被 execve 的文件必须留在 APK 的 `lib/` 下，其余可以下沉。所以最小占位包与常驻库 tar 是一对，缺了常驻库那份，装出来的包开不了机。

## 6. 发布页上的名字会短一截

上传到发布页时目录前缀会被去掉。索引里写 `dist/versebox-android9-1.0.1.zip`，发布页上看到的是 `versebox-android9-1.0.1.zip`。核对文件名时按最后一段比，不要按整串比。

## 7. 到手先做两件事

```bash
sha256sum -c SHA256SUMS
unzip -p versebox-android9-1.0.1.zip versebox_properties.yaml \
  | grep -E '^(id|romVersion|apiLevel|payloadSha256|payloadBytes):'
```

第一条校验收到的文件与清单逐个对上。第二条读 ROM 包自己的自述，`payloadSha256` 与索引里那一条相同，就说明这份包既没坏，也没被换过。
