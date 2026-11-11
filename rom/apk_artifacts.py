#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 VerseBox contributors
"""APK 侧产物（**每个运行时仓库各一份**，`docs/rom-release/01-入库配置/02-构建链产物.md`）。

一次 ROM 打包除了完整的 ROM zip 之外，还要产出两样**给 APK 用**的东西：

    <base>-min.zip                最小占位包：根目录只有 versebox_properties.yaml
                                  （payloadBytes: 0 / placeholder: true）
    apk-resident-libs-<id>.tar    APK lib/ 必须带的原生文件（被 execve 的那几个）

为什么是这两个（不是"顺手多打两个文件"）：

1. **最小占位包**让安装包可以只声明"我支持这个系统"，不背 payload（0.5–0.7 GB）；
   用户导入完整包之前，ROM 列表里那一版可见、不可用、且能说清原因。
2. **resident tar** 解决的是另一个硬约束：App 进程（`u:r:untrusted_app`）对
   `app_data_file` 没有 `execute_no_trans`，客体 linker / 属性播种器 / GPU 端点这些
   **被 execve 的文件**只能待在 APK 的 `nativeLibraryDir`（`lib/arm64-v8a`）。
   把它们打进 ROM 的 `payload/lib` 是对的（dlopen 集合能下沉），但 execve 集合
   必须同时有一份在 APK 里。

两个产物都从**已经打好的 zip** 反推，不重新读 prepared、不重新算哈希：
zip 里的自述与清单就是事实，多一条来源就多一个能漂移的地方。

用法：

    python rom/apk_artifacts.py --zip dist/versebox-android15-1.1.0.zip \
        --out dist --resident rom/resident-native.txt

`pack-rom.py` 里直接 import `build_apk_artifacts(...)`（同一份实现）。
"""

import argparse
import io
import json
import os
import sys
import tarfile
import zipfile

PROPERTIES = "versebox_properties.yaml"
DEFAULT_INPUTS = "versebox_inputs.json"


def _read_zip_text(archive, name):
    try:
        info = archive.getinfo(name)
    except KeyError:
        return None
    with archive.open(info) as handle:
        return handle.read().decode("utf-8", "replace")


def read_resident(path):
    """resident-native.txt：每行一个**逻辑名**（清单 files[].logical），# 之后是注释。"""
    names = set()
    if not path or not os.path.isfile(path):
        return names
    with io.open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if line:
                names.add(line)
    return names


def write_minimal_zip(source_zip, target):
    """把完整包的自述改写成占位自述，打成只含一个条目的 zip。"""
    with zipfile.ZipFile(source_zip, "r") as archive:
        text = _read_zip_text(archive, PROPERTIES)
    if text is None:
        raise RuntimeError("包内没有 %s：%s" % (PROPERTIES, source_zip))

    kept = []
    saw_placeholder = False
    for line in text.splitlines():
        key = line.split(":", 1)[0].strip()
        if key == "payloadBytes":
            kept.append("payloadBytes: 0")
        elif key == "placeholder":
            kept.append("placeholder: true")
            saw_placeholder = True
        else:
            kept.append(line)
    if not saw_placeholder:
        index = next((i for i, line in enumerate(kept)
                      if line.split(":", 1)[0].strip() == "payloadBytes"), -1)
        if index >= 0:
            kept.insert(index + 1, "placeholder: true")
        else:
            kept.append("placeholder: true")

    header = [
        "# 最小占位包（由 rom/apk_artifacts.py 生成，不是 ROM 发布的原件）：",
        "# 只用于让安装包的 ROM 列表里出现这一版；要真的开机必须导入完整包。",
    ]
    os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(PROPERTIES, "\n".join(header + kept) + "\n")
    return target


def collect_resident(source_zip, resident_names):
    """从 zip 里取出 resident 文件：[(归档内名字, 字节)]。

    判定只用清单：`files[].logical` 命中 resident 名单，且那条有 `libPath`
    （`libMode=apk` 的包没有 libPath，说明客体 .so 本来就住在 APK 的 lib/，不该在这里产出）。
    """
    picked = []
    with zipfile.ZipFile(source_zip, "r") as archive:
        properties = _read_zip_text(archive, PROPERTIES) or ""
        text = None
        for line in properties.splitlines():
            if line.strip().startswith("inputs:"):
                name = line.split(":", 1)[1].strip().strip('"').strip("'")
                if name:
                    text = _read_zip_text(archive, name)
                break
        if text is None:
            text = _read_zip_text(archive, DEFAULT_INPUTS)
        if not text:
            return picked
        manifest = json.loads(text)
        for item in manifest.get("files") or []:
            logical = item.get("logical")
            lib_path = item.get("libPath")
            if logical not in resident_names or not lib_path:
                continue
            try:
                info = archive.getinfo(lib_path)
            except KeyError:
                raise RuntimeError("清单说有 %s，包里却没有 %s" % (logical, lib_path))
            picked.append((os.path.basename(item.get("name") or lib_path),
                           archive.read(info)))
    return picked


def write_resident_tar(entries, target):
    """写 tar：平铺、0755（linker 要能 exec）。"""
    os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
    with tarfile.open(target, "w", format=tarfile.USTAR_FORMAT) as archive:
        for name, payload in entries:
            info = tarfile.TarInfo(name)
            info.size = len(payload)
            info.mode = 0o755
            info.mtime = 0
            archive.addfile(info, io.BytesIO(payload))
    return target


def build_apk_artifacts(zip_path, out_dir=None, resident_path=None, log=print):
    """产出 min zip + resident tar；返回结果字典（给 pack 报告用）。"""
    zip_path = os.path.abspath(zip_path)
    out_dir = os.path.abspath(out_dir or os.path.dirname(zip_path) or ".")
    base = os.path.splitext(os.path.basename(zip_path))[0]

    with zipfile.ZipFile(zip_path, "r") as archive:
        properties_text = _read_zip_text(archive, PROPERTIES) or ""
    values = {}
    for line in properties_text.splitlines():
        line = line.split("#", 1)[0].strip()
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    rom_id = values.get("id") or "rom"
    rom_version = values.get("romVersion") or "0"

    minimal = write_minimal_zip(zip_path, os.path.join(out_dir, base + "-min.zip"))

    resident_names = read_resident(resident_path or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "resident-native.txt"))
    entries = collect_resident(zip_path, resident_names)
    tar_path = os.path.join(out_dir, "apk-resident-libs-%s.tar" % rom_id)
    if entries:
        write_resident_tar(entries, tar_path)
    else:
        log("apk-artifacts: %s 没有 resident 文件（resident-native.txt 为空或清单没有 libPath）" % rom_id)
        tar_path = None

    result = {
        "minimal": minimal,
        "minimalBytes": os.path.getsize(minimal),
        "residentTar": tar_path,
        "residentLibs": [name for name, _ in entries],
    }
    log("apk-artifacts: %s@%s -> %s；resident %d 个%s" % (
        rom_id,
        rom_version,
        os.path.basename(minimal),
        len(entries),
        "" if not tar_path else "（%s）" % os.path.basename(tar_path)))
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description="产出 APK 侧两件产物（最小包 + resident tar）")
    parser.add_argument("--zip", required=True, help="已经打好的完整 ROM zip")
    parser.add_argument("--out", default=None, help="产物目录（默认与 zip 同目录）")
    parser.add_argument("--resident", default=None, help="resident-native.txt 路径")
    args = parser.parse_args(argv)
    try:
        result = build_apk_artifacts(args.zip, args.out, args.resident)
    except Exception as failure:  # noqa: BLE001 - CLI 只把原因打出来
        print("apk-artifacts 失败：%s" % failure)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
