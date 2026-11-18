# 发布

这一章约束 A9 运行时怎样发布一次：什么算一次发布，标题与正文从哪里来，产物版本与索引怎样对应，以及发布前要过哪些闸门。

顶层只放两样东西。构建产物与版本的说明写在这里，发布流程本身的五篇说明放在 FLOW 目录里。

| 入口 | 内容 |
| --- | --- |
| [ARTIFACTS.md](ARTIFACTS.md) | 产物清单、客体库装在哪一侧、两种哈希的区别 |
| [FLOW](FLOW/01-RELEASE-PIPELINE.md) | 流程五篇：流水线、标题与正文、索引、版本来源、验证与操作 |

## 这次落地了什么

全部改动都在本仓库。字节数与哈希是交付时的值。

| 路径 | 动作 | 字节 | sha256 |
| --- | --- | --- | --- |
| `.github/workflows/build.yml` | 修改 | 8917 | `0cdb88b6b2928ace624b2ffdb7d4a684fde02be1cddaddb3735574795e8ab691` |
| `.github/workflows/release-publish.yml` | 新增 | 11522 | `2183ecc80d8861d14d0576928d338af389387460bd03197fd3d9185df4792db7` |
| `.github/scripts/collect-release-assets.mjs` | 新增 | 2309 | `ab82241b5de3603aaf17a21d64273e158e723d8e0d5136bfb82c16c42f7b723d` |
| `.github/scripts/generate-release-notes.mjs` | 新增 | 6060 | `3b152e4dd0507c53220c3cc4b99f739b95006a522d718df7b3dd787bea63ac4c` |
| `.github/scripts/update-release-index.mjs` | 新增 | 4659 | `f6ddac0f4a5941fb6622f6777a0f9895ce1ea4a33839ac476bda038bf9df8a73` |
| `.github/scripts/lib/release-facts.mjs` | 新增 | 2485 | `65f5a6766bc59ced2eb31391ad945d102e532aad0087736e97b9f357e32e9250` |
| `release/release.json` | 新增 | 3 | `37517e5f3dc66819f61f5a7bb8ace1921282415f10551d2defa5c3eb0985b570` |
| `rom/resident-native.txt` | 修改注释 | 2520 | `93100e319fd4446f35a94ad6d314c17d1e3080da415296022b80c246db7e8905` |

构建工作流只改发布相关的部分。原先"打标签顺手建发布页"的步骤整段删掉，权限从写降为只读，推送触发加了索引目录的跳过规则。构建本身、缓存键、产物校验与附件名称都没有动，因为那条路径已经跑通，换掉它等于把已有的证据作废。

构建侧另外三处小改，理由都写在[05-VERIFICATION](FLOW/05-VERIFICATION.md) 第 2 节。一处是删掉一个没有任何脚本读取的环境变量，一处是给 `d8` 补一条存在性断言，一处是让哈希清单里每个文件只出现一行。

发布工作流是新增的入口，四道闸门与生成、写回两组步骤按顺序排开。它由标签构建的完成事件自动拉起，成功且是标签推送才继续，失败或分支构建停在入口条件上；同一条工作流也保留手动触发，供补发与重试。三个脚本各管一件事，收集附件哈希、生成标题与正文、写入版本索引，需要共同读取的事实在 `lib/release-facts.mjs` 里读一次，避免三处各读一份之后说法不一致。

## 与其它文档的关系

宿主工程里有三份说明与这里的判据相关，分别是 `docs/android9-port/11-阶段6-打包与回归.md`、`docs/android9-port/27-ROM包与客体so下沉.md` 与 `docs/rom-package/05-构建链改造.md`。它们描述的是宿主侧与打包器，规则发生变化时，本目录 FLOW 里的闸门要一起复核。

产物同时受两条链路约束。准备清单决定运行时里有什么，ROM 包决定宿主导入的是什么，两者都由本仓库构建，所以改动的判据也都在本仓库的 CI 里。

打包器的用法与自检口径写在 `rom/README.md`，那一份管"包怎么打"，本目录管"打出来的东西怎么发"。
