#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 VerseBox contributors
"""VerseBox ROM 打包器（**Android 9 域**，`docs/rom-package/05-构建链改造.md` 的实现）。

职责只有三件：**拼目录、改清单、算哈希**。它不重新生成任何内容，也不改
`scripts/prepare-a9.py` 的来源与闸门。

产物结构（`docs/rom-package/02-ROM包结构与命名.md`）：

    versebox_properties.yaml     ROM 自述（zip 根，唯一 yaml）
    versebox_inputs.json         运行时清单（zip 根，只改两类字段）
    payload/                     客体内容（原 assets/guest-runtime-a9/** 整树 + lib/）

## 为什么本仓库自带一份打包器（不要再"复用" A15 的那份）

**A9 / A10 / A15 是三个独立的运行时仓库**（`docs/rom-package/11` §1）。它们在本机碰巧
放在同一个 app 工程里，但它们各自被 clone、各自 CI、各自发布；跨仓库引用脚本等于
"只 clone A9 的人打不出 A9 的 ROM"，也等于把两条发布节奏绑在一起。所以：

| 东西 | 归属 |
| --- | --- |
| 打包器 / `rom_hash.py` / APK 侧 stager | 各运行时仓库自己的 `rom/` 目录 |
| ROM 的**契约**（zip 结构、`payloadSha256` 算法、清单改写规则） | `docs/rom-package/02`、`03` —— 三域共用**同一份合同**，不是同一份文件 |

合同的执法点是**算法**而不是文件路径：`rom_hash.py` 与 `payloadSha256` 的规范化算法
必须与 app 侧解包器逐字节一致（`docs/rom-package/02` §6.2），打包后自检 `--verify-only`
与 app 的 L2 校验会同时验证这一点。改这一份时必须同步 `guest-runtime`/`guest-runtime-a15`
的同名实现，否则解包后的自证会假失败。

用法（在 `guest-runtime-a9` 根目录）：

    # 默认：用 rom/rom.properties.yaml 作为 ROM 自述，产物落到 dist/
    python rom/pack-rom.py --prepared prepared

    # 指定版本 / 客体 .so 的承载方式（apk = 客体 .so 仍留 APK，只把数据装进 ROM）
    python rom/pack-rom.py --prepared prepared --rom-version 1.1.0 --lib-mode apk

    # 只校验一个已有包（不需要 prepared）
    python rom/pack-rom.py --verify-only dist/versebox-android9-1.0.0.zip
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import shutil
import sys
import tempfile
import time
import uuid
import zipfile

from apk_artifacts import build_apk_artifacts

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rom_hash import payload_entries, payload_sha256  # noqa: E402  (本仓库唯一哈希实现)

ROM_SCHEMA = "versebox-rom/2"
ASSETS_NAMES = ("versebox_properties.yaml", "versebox_inputs.json")
PAYLOAD_ROOT = "payload/"
REQUIRED_PROPERTIES = ("schema", "id", "romVersion", "apiLevel", "inputs", "payloadRoot",
                       "payloadBytes", "payloadSha256")
LIB_MODES = ("rom", "apk", "both")
# 只认本域的 assets 前缀：A9 的打包器不许把 A10/A15 的树打成 A9 的 ROM
# （`_audit/00` §4.5：一个域的东西只出现在这个域里）。
SOURCE_PREFIXES = ("guest-runtime-a9/",)
FIXED_ZIP_DATE = (2026, 1, 1, 0, 0, 0)
HERE = os.path.dirname(os.path.abspath(__file__))
# ROM 的「身份」跟着本仓库走（id / 名字 / 默认版本），不跟 app 的 assets 走。
DEFAULT_PROPERTIES = os.path.join(HERE, "rom.properties.yaml")
MODULE_DIR = os.path.dirname(HERE)
DEFAULT_DIST = os.path.join(MODULE_DIR, "dist")


RESIDENT_FILE = os.path.join(HERE, "resident-native.txt")


def read_resident(path=RESIDENT_FILE):
    """只能从 APK 的 lib/ execve 的逻辑名（与 rom/stage-apk-payload.py 共用这一份清单）。

    它们仍然随包（ROM 要自描述、也要能校验），但运行时**必须**用 APK 里那份去 execve：
    untrusted_app 对 app_data_file 没有 execute_no_trans（实测 error=13），见 resident-native.txt。
    """
    names = set()
    if not os.path.isfile(path):
        return names
    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if line:
                names.add(line)
    return names


class PackError(Exception):
    pass


# --------------------------------------------------------------------- yaml

def read_properties(path):
    """平铺 `key: value` 解析（与 RomCatalog.yaml() 同一套规则，不引入 YAML 依赖）。"""
    values = {}
    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            separator = line.find(":")
            if separator <= 0:
                continue
            key = line[:separator].strip()
            value = line[separator + 1:].strip().strip('"').strip("'")
            if key:
                values[key] = value
    return values


PROPERTY_ORDER = (
    "schema", "id", "romVersion", "name", "androidVersion", "apiLevel", "description",
    "inputs", "payloadRoot", "payloadBytes", "payloadSha256", "libMode",
    "packedBy", "packedAt",
)


def write_properties(path, values):
    lines = ["# VerseBox ROM 自述。由 rom/pack-rom.py（Android 9 运行时仓库）生成，不要手改。",
             "# 结构规范见 docs/rom-package/02-ROM包结构与命名.md"]
    written = set()
    for key in PROPERTY_ORDER:
        if key in values:
            lines.append("%s: %s" % (key, values[key]))
            written.add(key)
    for key in sorted(values):
        if key not in written:
            lines.append("%s: %s" % (key, values[key]))
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


# ---------------------------------------------------------------- manifest

def normalize_prefix(prefix):
    if not prefix:
        raise PackError("缺少 assets 前缀（--assets-prefix / 清单 assetPrefix）")
    return prefix if prefix.endswith("/") else prefix + "/"


def rewrite_manifest(manifest, prefix, lib_mode):
    """只做两类改写（docs/rom-package/03 §1.1）；其余键值逐字不动。"""
    data = manifest.get("data")
    if not isinstance(data, list) or not data:
        raise PackError("清单缺少 data[]")
    rewritten = []
    for item in data:
        name = item.get("name")
        if not isinstance(name, str) or not name:
            raise PackError("data[] 里有一条缺少 name")
        if not name.startswith(prefix):
            raise PackError("data[].name 不以 %s 开头：%s" % (prefix, name))
        entry = dict(item)
        entry["name"] = PAYLOAD_ROOT + name[len(prefix):]
        # installPath 是「相对实例目录」的目标路径，不参与改写（docs/rom-package/03 §1.1）。
        rewritten.append(entry)

    files = manifest.get("files")
    if not isinstance(files, list):
        raise PackError("清单缺少 files[]")
    resident = read_resident()
    rewritten_files = []
    for item in files:
        name = item.get("name")
        if not isinstance(name, str) or not name:
            raise PackError("files[] 里有一条缺少 name")
        entry = dict(item)
        if lib_mode == "apk":
            entry.pop("libPath", None)
        else:
            entry["libPath"] = PAYLOAD_ROOT + "lib/" + name
        if item.get("logical") in resident:
            entry["resident"] = True
        rewritten_files.append(entry)

    result = dict(manifest)
    result["data"] = rewritten
    result["files"] = rewritten_files
    result.pop("assetPrefix", None)   # 新清单不再有 assets 前缀这个概念
    return result


def assert_rewrite_is_lossless(before, after, lib_mode):
    """自证：除被改写的键之外，其余值必须完全相等（docs/rom-package/03 §1.1 判据）。"""
    for key, value in before.items():
        if key in ("data", "files", "assetPrefix"):
            continue
        if key not in after or after[key] != value:
            raise PackError("改写器动了不该动的键：%s" % key)
    for index, item in enumerate(before["data"]):
        for key, value in item.items():
            if key == "name":
                continue
            if after["data"][index].get(key) != value:
                raise PackError("data[%d].%s 被改写" % (index, key))
    for index, item in enumerate(before["files"]):
        for key, value in item.items():
            if key in ("libPath", "resident"):
                continue
            if after["files"][index].get(key) != value:
                raise PackError("files[%d].%s 被改写" % (index, key))
    if lib_mode == "apk" and any("libPath" in item for item in after["files"]):
        raise PackError("libMode=apk 的清单不该写 libPath")


# -------------------------------------------------------------------- pack

def copy_payload(prepared, prefix, payload_dir):
    source = os.path.join(prepared, "assets", prefix.rstrip("/"))
    if not os.path.isdir(source):
        raise PackError("找不到 payload 来源目录：%s" % source)
    shutil.copytree(source, payload_dir, dirs_exist_ok=True)
    for root, dirs, files in os.walk(payload_dir):
        for name in files:
            path = os.path.join(root, name)
            if os.path.islink(path):
                raise PackError("payload 里出现符号链接（zip 表达不可靠）：%s" % path)
    return source


def copy_native_libs(prepared, payload_dir):
    source = os.path.join(prepared, "jniLibs", "arm64-v8a")
    if not os.path.isdir(source):
        raise PackError("找不到 jniLibs 目录：%s" % source)
    target = os.path.join(payload_dir, "lib")
    os.makedirs(target, exist_ok=True)
    copied = 0
    for name in sorted(os.listdir(source)):
        path = os.path.join(source, name)
        if not os.path.isfile(path):
            continue
        shutil.copyfile(path, os.path.join(target, name))
        copied += 1
    return copied


def new_stage_dir(parent, mode):
    """在 parent 下造一个新的暂存目录（随机后缀，撞名就再来一次）。"""
    for _ in range(64):
        path = os.path.join(parent, "versebox-rom-stage-%s" % uuid.uuid4().hex[:8])
        try:
            os.mkdir(path, mode)
            return path
        except FileExistsError:
            continue
    raise PackError("无法在 %s 下创建唯一的暂存目录" % parent)


def make_stage(fallback_parent):
    """暂存目录：系统临时目录优先，被拒时退回**产物同卷**的仓库目录。

    两件真实约束（本机 Windows + 受限沙箱实测，2026-11-12）：

    1. 默认用 **0700**：POSIX 上暂存里有整份 payload，不该让别的用户读；
    2. 但受限沙箱里"在 0700 的新目录下再建子目录"会被拒（`PermissionError`，
       `WinError 5`），连事后 `chmod` 都被拒。只有**建的时候**给 0777 才能继续。
       所以第二顺位才是 0777，并且**打印一行**说明用了它——不静默降低权限。

    回退点选"产物同卷的仓库目录"：跨卷暂存会把 1.2 GB 复制两遍。
    四处组合都被拒时**直接失败并说清是哪儿**，不静默换到别的地方写。
    """
    tried = []
    for parent in (None, fallback_parent):
        base = parent or tempfile.gettempdir()
        for mode in (0o700, 0o777):
            stage = None
            try:
                stage = new_stage_dir(base, mode)
                os.makedirs(os.path.join(stage, "payload"), exist_ok=True)
                if mode != 0o700:
                    print("暂存目录用 0777（宿主沙箱不允许在 0700 目录下建子目录）：%s" % stage)
                return stage
            except OSError:
                tried.append("%s(mode=%o)" % (base, mode))
                if stage:
                    shutil.rmtree(stage, ignore_errors=True)
    raise PackError("无法创建暂存目录（都被拒绝写入：%s）" % "、".join(tried))


def write_zip(stage, out_path):
    entries = payload_entries(os.path.join(stage, "payload"))
    with zipfile.ZipFile(out_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name in ASSETS_NAMES:
            info = zipfile.ZipInfo(name, date_time=FIXED_ZIP_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(os.path.join(stage, name), "rb") as handle:
                archive.writestr(info, handle.read())
        info = zipfile.ZipInfo(PAYLOAD_ROOT, date_time=FIXED_ZIP_DATE)
        info.compress_type = zipfile.ZIP_STORED
        info.external_attr = (0o40755 << 16) | 0x10
        archive.writestr(info, b"")
        for rel, absolute in entries:
            info = zipfile.ZipInfo(PAYLOAD_ROOT + rel, date_time=FIXED_ZIP_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(absolute, "rb") as handle:
                archive.writestr(info, handle.read())


def resolve_prefix(manifest, properties, override):
    if override:
        return normalize_prefix(override)
    prefix = manifest.get("assetPrefix") or properties.get("assetPrefix")
    if prefix:
        return normalize_prefix(prefix)
    for candidate in SOURCE_PREFIXES:
        if properties.get("id") == candidate.rstrip("/").replace("guest-runtime", "android"):
            return candidate
    raise PackError("清单没有 assetPrefix，且未给 --assets-prefix")


def pack(args):
    prepared = os.path.abspath(args.prepared)
    if not os.path.isdir(prepared):
        raise PackError("prepared 目录不存在：%s" % prepared)
    probe = os.path.join(prepared, "assets", args.probe)
    if not os.path.isfile(probe):
        raise PackError("找不到清单：%s" % probe)
    properties_path = args.properties or DEFAULT_PROPERTIES
    if not os.path.isfile(properties_path):
        raise PackError("找不到 ROM 自述：%s" % properties_path)

    with open(probe, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    properties = read_properties(properties_path)

    rom_id = properties.get("id")
    if not rom_id:
        raise PackError("ROM 自述缺少 id：%s" % properties_path)
    # apiLevel 的唯一来源是**准备好的清单**（它来自 prepare-a9.py，是产物事实）；
    # 自述里写了就必须与之一致，不写也不影响——这样第三方改 ROM 不用手抄 API 号。
    api_level = int(manifest.get("guestSdk") or -1)
    if api_level < 26 or api_level > 35:
        raise PackError("清单 guestSdk 不合法：%r" % manifest.get("guestSdk"))
    declared_api = properties.get("apiLevel")
    if declared_api and int(declared_api) != api_level:
        raise PackError("自述 apiLevel=%s 与清单 guestSdk=%s 不一致" % (declared_api, api_level))

    lib_mode = args.lib_mode or properties.get("libMode") or "apk"
    if lib_mode not in LIB_MODES:
        raise PackError("--lib-mode 只能是 %s" % (LIB_MODES,))

    prefix = resolve_prefix(manifest, properties, args.assets_prefix)
    staged_manifest = rewrite_manifest(manifest, prefix, lib_mode)
    assert_rewrite_is_lossless(manifest, staged_manifest, lib_mode)

    stage = make_stage(MODULE_DIR)
    try:
        payload_dir = os.path.join(stage, "payload")
        os.makedirs(payload_dir, exist_ok=True)
        copy_payload(prepared, prefix, payload_dir)
        lib_count = 0
        if lib_mode != "apk":
            lib_count = copy_native_libs(prepared, payload_dir)

        actual = {rel for rel, _ in payload_entries(payload_dir)}
        missing = [item["name"] for item in staged_manifest["data"]
                   if item["name"].replace(PAYLOAD_ROOT, "", 1) not in actual]
        if missing:
            raise PackError("清单里有 %d 条在 payload 里找不到，例如：%s"
                            % (len(missing), missing[0]))
        if lib_count:
            for item in staged_manifest["files"]:
                if item["libPath"].replace(PAYLOAD_ROOT, "", 1) not in actual:
                    raise PackError("清单 files[] 的 libPath 缺失：%s" % item["libPath"])

        payload_hash, file_count, payload_bytes = payload_sha256(payload_dir)
        with open(os.path.join(stage, "versebox_inputs.json"), "w",
                  encoding="utf-8", newline="\n") as handle:
            json.dump(staged_manifest, handle, ensure_ascii=False, separators=(",", ":"))

        rom_version = (args.rom_version or properties.get("romVersion")
                       or properties.get("defaultRomVersion") or "1.0.0")
        write_properties(os.path.join(stage, "versebox_properties.yaml"), {
            "schema": ROM_SCHEMA,
            "id": rom_id,
            "romVersion": rom_version,
            "name": properties.get("name") or rom_id,
            "androidVersion": properties.get("androidVersion") or "",
            "apiLevel": api_level,
            "description": properties.get("description") or "",
            "inputs": "versebox_inputs.json",
            "payloadRoot": PAYLOAD_ROOT,
            "payloadBytes": payload_bytes,
            "payloadSha256": payload_hash,
            "libMode": lib_mode,
            "packedBy": "rom/pack-rom.py@%s" % (args.git_rev or "rev-unknown"),
            "packedAt": datetime.datetime.now(datetime.timezone.utc)
                                  .strftime("%Y-%m-%dT%H:%M:%SZ"),
        })

        out = os.path.abspath(args.out or os.path.join(
            DEFAULT_DIST, "versebox-%s-%s.zip" % (rom_id, rom_version)))
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        started = time.time()
        write_zip(stage, out)
        took = time.time() - started

        problems = verify_zip(out)
        if problems:
            os.remove(out)
            raise PackError("包内自检失败，未产出 zip：\n  - " + "\n  - ".join(problems))

        report = {
            "romKey": "%s@%s" % (rom_id, rom_version),
            "out": out,
            "zipBytes": os.path.getsize(out),
            "payloadBytes": payload_bytes,
            "payloadSha256": payload_hash,
            "payloadFiles": file_count,
            "manifestFiles": len(staged_manifest["files"]),
            "manifestData": len(staged_manifest["data"]),
            "libMode": lib_mode,
            "bundledLibs": lib_count,
            "packSeconds": round(took, 1),
        }
        # ------------------------------------------------------ APK 侧两件产物
        #
        # 完整 zip 是"ROM 本体"，安装包还需要另外两样（见 rom/apk_artifacts.py 的说明）：
        #   -min.zip   最小占位包（只声明"支持这个系统"）
        #   apk-resident-libs-<id>.tar   APK lib/ 必须带的原生文件
        # 两者都从这份 zip 反推，失败就让整次打包失败——不能产出"能装不能开机"的组合。
        apk_artifacts = build_apk_artifacts(out, os.path.dirname(out))
        report["minimalZip"] = os.path.basename(apk_artifacts["minimal"])
        report["minimalBytes"] = apk_artifacts["minimalBytes"]
        report["residentTar"] = (os.path.basename(apk_artifacts["residentTar"])
                                 if apk_artifacts["residentTar"] else None)
        report["residentLibs"] = apk_artifacts["residentLibs"]

        report_path = os.path.join(os.path.dirname(out),
                                   "pack-%s-%s.json" % (rom_id, rom_version))
        with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(report, handle, ensure_ascii=False, indent=2)
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0
    finally:
        shutil.rmtree(stage, ignore_errors=True)


# ------------------------------------------------------------------ verify

def verify_zip(path):
    """返回问题列表；空列表 = 通过（`docs/rom-package/05` §6 的全部检查）。"""
    problems = []
    try:
        archive = zipfile.ZipFile(path, "r")
    except Exception as failure:  # noqa: BLE001 - 任何打不开都算不合法
        return ["不是可读的 zip：%s（%s）" % (path, failure)]

    with archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        roots = {name.split("/", 1)[0] for name in names if name}
        expected_roots = {"versebox_properties.yaml", "versebox_inputs.json", "payload"}
        if roots != expected_roots:
            problems.append("根条目不是恰好三项：%s" % sorted(roots))

        for name in names:
            if name.startswith("/") or ".." in name.split("/") or ":" in name:
                problems.append("非法条目（绝对路径/上跳/盘符）：%s" % name)
        for info in infos:
            if (info.external_attr >> 16) & 0o170000 == 0o120000:
                problems.append("包含符号链接：%s" % info.filename)
        # 只禁止 payload/ 根下的 *.apex 原文件（A15 的 apex 必须已是展开目录）。
        # rootfs/ 内部自带的 vendor apex 是镜像内容，属于客体文件系统的一部分，
        # 不能当"打包错误"（实测 A15 镜像里有 1 个，A9 的镜像里也可能有）。
        misplaced_apex = [name for name in names
                          if name.lower().endswith(".apex") and name.count("/") == 1]
        if misplaced_apex:
            problems.append("payload 根下有 *.apex 原文件（应只带展开后的 apex/ 目录）：%s"
                            % misplaced_apex[0])

        try:
            raw_properties = archive.read("versebox_properties.yaml").decode("utf-8")
        except KeyError:
            return problems + ["缺少 versebox_properties.yaml"]
        properties = {}
        for line in raw_properties.splitlines():
            line = line.split("#", 1)[0].strip()
            if not line or ":" not in line:
                continue
            key, value = line.split(":", 1)
            properties[key.strip()] = value.strip().strip('"').strip("'")

        for key in REQUIRED_PROPERTIES:
            if not properties.get(key):
                problems.append("versebox_properties.yaml 缺少字段：%s" % key)
        if properties.get("schema") != ROM_SCHEMA:
            problems.append("schema 不是 %s：%r" % (ROM_SCHEMA, properties.get("schema")))
        try:
            api_level = int(properties.get("apiLevel") or 0)
            if api_level < 26 or api_level > 35:
                problems.append("apiLevel 超出 26..35：%s" % api_level)
        except ValueError:
            api_level = 0
            problems.append("apiLevel 不是数字：%r" % properties.get("apiLevel"))
        if properties.get("payloadRoot") != PAYLOAD_ROOT:
            problems.append("payloadRoot 必须是 payload/：%r" % properties.get("payloadRoot"))
        sha = properties.get("payloadSha256") or ""
        if len(sha) != 64 or any(ch not in "0123456789abcdef" for ch in sha.lower()):
            problems.append("payloadSha256 不是 64 位十六进制：%r" % sha)
        try:
            declared_bytes = int(properties.get("payloadBytes") or 0)
        except ValueError:
            declared_bytes = 0
            problems.append("payloadBytes 不是数字")

        manifest = {}
        inputs = properties.get("inputs") or "versebox_inputs.json"
        try:
            manifest = json.loads(archive.read(inputs).decode("utf-8"))
        except KeyError:
            problems.append("缺少清单：%s" % inputs)
        except Exception as failure:  # noqa: BLE001
            problems.append("清单不是合法 JSON：%s" % failure)

        if manifest:
            if int(manifest.get("guestSdk") or -1) != api_level:
                problems.append("清单 guestSdk=%s 与 apiLevel=%s 不一致"
                                % (manifest.get("guestSdk"), api_level))
            nameset = set(names)
            for index, item in enumerate(manifest.get("data") or []):
                name = item.get("name") or ""
                if not name.startswith(PAYLOAD_ROOT):
                    problems.append("data[%d].name 不以 payload/ 开头：%r" % (index, name))
                elif name not in nameset:
                    problems.append("data[%d] 在 zip 里找不到：%s" % (index, name))
            lib_mode = properties.get("libMode") or "rom"
            for index, item in enumerate(manifest.get("files") or []):
                lib_path = item.get("libPath")
                if lib_mode != "apk":
                    if not lib_path:
                        problems.append("files[%d] 缺少 libPath（libMode=%s）" % (index, lib_mode))
                    elif lib_path not in nameset:
                        problems.append("files[%d].libPath 在 zip 里找不到：%s" % (index, lib_path))

        actual_bytes = sum(info.file_size for info in infos
                           if info.filename.startswith(PAYLOAD_ROOT) and not info.is_dir())
        if declared_bytes and declared_bytes != actual_bytes:
            problems.append("payloadBytes 不符：声明 %d，实测 %d" % (declared_bytes, actual_bytes))
    return problems


def verify_only(path):
    problems = verify_zip(path)
    if problems:
        print("ROM 包不合法 %s：" % path)
        for problem in problems:
            print("  - %s" % problem)
        return 1
    print("ROM 包校验通过：%s（%.1f MB）" % (path, os.path.getsize(path) / 1e6))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="VerseBox ROM 打包器（Android 9）")
    parser.add_argument("--prepared")
    parser.add_argument("--probe", default=None)
    parser.add_argument("--properties", default=None,
                        help="ROM 自述；默认用同目录的 rom.properties.yaml")
    parser.add_argument("--assets-prefix", default=None)
    parser.add_argument("--rom-version", default=None)
    parser.add_argument("--lib-mode", default=None, choices=LIB_MODES,
                        help="客体 .so 的承载方式；默认取自述里的 libMode，再退回 apk")
    parser.add_argument("--out", default=None)
    parser.add_argument("--git-rev", default=None)
    parser.add_argument("--verify-only", default=None)
    args = parser.parse_args(argv)

    try:
        if args.verify_only:
            return verify_only(args.verify_only)
        if not args.prepared:
            parser.error("打包需要 --prepared <prepared 目录>")
        if not args.probe:
            candidates = [name for name in sorted(os.listdir(os.path.join(args.prepared, "assets")))
                          if name.endswith("-inputs.json")]
            if len(candidates) != 1:
                parser.error("无法自动确定清单，请用 --probe 指定（候选：%s）" % candidates)
            args.probe = candidates[0]
        return pack(args)
    except PackError as failure:
        print("打包失败：%s" % failure)
        return 1


if __name__ == "__main__":
    sys.exit(main())