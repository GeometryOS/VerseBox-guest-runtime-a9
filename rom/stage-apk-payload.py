#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 VerseBox contributors
"""把 prepared/ 变成「打进 APK 的旧格式 payload」（Android 9 域）。

## 这是什么

客体 .so 有两种承载方式（docs/rom-package/04-APK与ROM的边界.md §4）：

| 格式 | 客体 .so 在哪 | 谁产出 |
| --- | --- | --- |
| 新格式（ROM 包 zip） | `payload/lib/<name>`（清单 `libPath`） | `rom/pack-rom.py` |
| 旧格式（打进 APK） | `assets/<prefix>/lib/<name>`（清单 `libPath`） | **本脚本** |

**两种格式都不把客体 .so 放在 APK 的 `lib/arm64-v8a/`**：那个目录只留宿主的库与
`rom/resident-native.txt` 里那几个**被 execve** 的文件（A9 是 2 个：`ld-android.so`、
`seed-properties`）。平台事实：App 进程在 `u:r:untrusted_app`，对 `app_data_file` 有
`execute` 但没有 `execute_no_trans`，所以 `dlopen`/`mmap` 允许、`execve` 会 `EACCES`
（error=13）；APK 自带的 `nativeLibraryDir` 标签不同，`execve` 允许。
客体 .so 一旦全留在 `lib/`，就等于「一张 APK 背一个系统」，而且每次 `adb install -r`
换 install 目录、实例里上千个软链全悬空
（_audit/coding-log/2026-11-08-.../14-没意识到重装APK会换install目录.md）。

## 输入 / 输出

    python rom/stage-apk-payload.py --prepared prepared --out prepared-apk

    <out>/assets/<prefix>/rootfs/...          客体文件系统（与 prepared 逐字相同）
    <out>/assets/<prefix>/rom-encoded/...     大小写/后缀冲突的转义副本（沿用现有约定）
    <out>/assets/<prefix>/lib/<name>          **客体 .so**（原来在 prepared/jniLibs/arm64-v8a）
    <out>/assets/<prefix>/*.dex、guest.prop
    <out>/assets/<manifest>.json              清单：只多一个 files[].libPath

<out>/jniLibs/arm64-v8a/ 里**只有** resident 那几个（A9 = 2 个）：这正是
「APK 的 lib 目录里不再有客体 .so」的判据，也是 :guest-runtime-a9 的构建闸门。

## 三条硬规则

1. **不改内容、不改哈希**：只搬文件、只给 `files[]` 加 `libPath`；`data[]` 一个字节都不动
   （`installPath` 仍由运行时按 `assetPrefix` 推出来，见 RuntimeFiles.installData()）。
2. **产物可自证**：每条 `files[]` 的字节数与 SHA-256 必须与 `prepared/jniLibs` 里的原件一致，
   对不上就**不产出**（_audit/00 §4.4：构建条件缺失就失败，不许回退）。
3. **同卷用硬链接**：`prepared` 与 `prepared-apk` 在同一卷时用 `os.link`，把 1.2 GB 的中转
   成本降为 0；跨卷才复制。硬链接不改变可执行位，也不产生第二份字节。

Gradle（`:guest-runtime-a9`）消费的是 `--out` 这个目录，不是 `prepared`：
`verifyRuntimeA9` 按 `libPath` 逐条核对 bytes/SHA-256，并断言 `jniLibs` 里的 .so 恰好
等于 resident 集合（多一个少一个都构建失败）。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys


HERE = os.path.dirname(os.path.abspath(__file__))
RESIDENT_FILE = os.path.join(HERE, "resident-native.txt")


def read_resident(path=RESIDENT_FILE):
    """只能从 APK 的 lib/ execve 的逻辑名（与 rom/pack-rom.py 共用这一份清单）。"""
    names = set()
    if not os.path.isfile(path):
        return names
    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if line:
                names.add(line)
    return names


class StageError(Exception):
    pass


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def place(source, target):
    """同卷硬链接优先，跨卷退回复制；目标已是最新就跳过。返回 'link'/'copy'/'skip'。"""
    if os.path.isfile(target) and not os.path.islink(target):
        try:
            same = os.path.samefile(source, target)
        except OSError:
            same = False
        if same:
            return "skip"
        if (os.path.getsize(target) == os.path.getsize(source)
                and os.path.getmtime(target) >= os.path.getmtime(source)):
            return "skip"
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if os.path.exists(target):
        os.remove(target)
    try:
        os.link(source, target)
        return "link"
    except OSError:
        shutil.copyfile(source, target)
        return "copy"


def stage(prepared, out, manifest_name, prefix):
    assets = os.path.join(prepared, "assets")
    source_manifest = os.path.join(assets, manifest_name)
    if not os.path.isfile(source_manifest):
        raise StageError("找不到清单：%s（先跑 scripts/prepare-a15.py）" % source_manifest)
    payload = os.path.join(assets, prefix.rstrip("/"))
    if not os.path.isdir(payload):
        raise StageError("找不到 payload 来源目录：%s" % payload)
    jni = os.path.join(prepared, "jniLibs", "arm64-v8a")
    if not os.path.isdir(jni):
        raise StageError("找不到 jniLibs 目录：%s" % jni)

    with open(source_manifest, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if manifest.get("assetPrefix") != prefix:
        raise StageError("清单 assetPrefix=%r 与 --prefix=%r 不一致"
                         % (manifest.get("assetPrefix"), prefix))
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise StageError("清单缺少 files[]")

    out_assets = os.path.join(out, "assets")
    out_payload = os.path.join(out_assets, prefix.rstrip("/"))
    out_lib = os.path.join(out_payload, "lib")
    # payload/lib 每次重算：清单里"谁能被 dlopen"变了的话，旧的多余文件必须消失，
    # 否则 ROM/APK 会出现"清单没写、文件却在"的第二份真相。
    if os.path.isdir(out_lib):
        shutil.rmtree(out_lib)
    os.makedirs(out_lib, exist_ok=True)
    # AGP 的 jniLibs 源目录要带 ABI 层：<dir>/jniLibs/arm64-v8a/*.so。
    # 里面只允许 resident（被 execve 的）那几个；其余 1000+ 个客体 .so 不在 APK 的 lib/ 里。
    out_jni = os.path.join(out, "jniLibs", "arm64-v8a")
    if os.path.isdir(os.path.join(out, "jniLibs")):
        shutil.rmtree(os.path.join(out, "jniLibs"))
    os.makedirs(out_jni, exist_ok=True)

    # 1) 客体文件系统整树（rootfs / rom-encoded / dex / guest.prop）
    placed = {"link": 0, "copy": 0, "skip": 0}
    for root, _dirs, names in os.walk(payload):
        for name in names:
            source = os.path.join(root, name)
            relative = os.path.relpath(source, payload)
            target = os.path.join(out_payload, relative)
            if relative.split(os.sep)[0] == "lib":
                raise StageError("prepared 的 payload 里已经有 lib/（来源不合法）：%s" % source)
            placed[place(source, target)] += 1

    # 2) 客体 .so：能被 dlopen 的进 assets/<prefix>/lib/；**被 execve 的留在 jniLibs/**
    #    （untrusted_app 对 app_data_file 没有 execute_no_trans，见 rom/resident-native.txt）
    resident = read_resident()
    lib_bytes = 0
    packed_count = 0
    for index, item in enumerate(files):
        name = item.get("name")
        if not isinstance(name, str) or not name:
            raise StageError("files[%d] 缺少 name" % index)
        source = os.path.join(jni, name)
        if not os.path.isfile(source):
            raise StageError("files[%d] 在 jniLibs 里找不到：%s" % (index, name))
        if os.path.getsize(source) != item["bytes"]:
            raise StageError("files[%d] %s 字节数不符（清单 %d，实际 %d）"
                             % (index, name, item["bytes"], os.path.getsize(source)))
        if sha256(source) != item["sha256"]:
            raise StageError("files[%d] %s 的 SHA-256 与清单不符（prepared 有问题）" % (index, name))
        if item.get("logical") in resident:
            # 必须留在 APK 的 lib/：execve 只能从这里成功。
            place(source, os.path.join(out_jni, name))
            item["resident"] = True
        else:
            place(source, os.path.join(out_lib, name))
            packed_count += 1
        lib_bytes += item["bytes"]
        item["libPath"] = prefix + "lib/" + name

    # 3) 清单：只多 libPath；其余键值逐字不动
    staged = dict(manifest)
    staged["files"] = files
    staged_path = os.path.join(out_assets, manifest_name)
    os.makedirs(os.path.dirname(staged_path), exist_ok=True)
    with open(staged_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(staged, handle, ensure_ascii=False, separators=(",", ":"))

    # 4) 自证：清单每条 data[]/files[] 都能在产物里找到（大小写敏感）
    present = set()
    for root, _dirs, names in os.walk(out_payload):
        for name in names:
            relative = os.path.relpath(os.path.join(root, name), out_payload).replace(os.sep, "/")
            present.add(prefix + relative)
    missing = [item["name"] for item in manifest.get("data", []) if item["name"] not in present]
    if missing:
        raise StageError("清单 data[] 有 %d 条不在产物里，例如：%s" % (len(missing), missing[0]))
    for item in files:
        if item.get("resident"):
            continue
        if item["libPath"] not in present:
            raise StageError("libPath 不在产物里：%s" % item["libPath"])
    listed = {item["name"] for item in files if item.get("resident")}
    unexpected = sorted(n for n in os.listdir(out_jni) if n.endswith(".so") and n not in listed)
    if unexpected:
        raise StageError("APK 的 lib 目录里出现了不该有的客体 .so：%s" % unexpected[0])
    if len(listed) != len([n for n in os.listdir(out_jni) if n.endswith(".so")]):
        raise StageError("jniLibs 里的 .so 数量与 resident 清单不一致")

    return {
        "out": os.path.abspath(out),
        "manifest": manifest_name,
        "prefix": prefix,
        "files": len(files),
        "resident": len(resident),
        "payloadLibs": packed_count,
        "libBytes": lib_bytes,
        "data": len(manifest.get("data", [])),
        "placed": placed,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description="把 prepared 变成 APK 内旧格式 payload")
    parser.add_argument("--prepared", default="prepared")
    parser.add_argument("--out", default="prepared-apk")
    parser.add_argument("--manifest", default="guest-runtime-a9-inputs.json")
    parser.add_argument("--prefix", default="guest-runtime-a9/")
    args = parser.parse_args(argv)
    try:
        report = stage(os.path.abspath(args.prepared), os.path.abspath(args.out),
                       args.manifest, args.prefix)
    except StageError as failure:
        print("旧格式 payload 生成失败：%s" % failure)
        return 1
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
