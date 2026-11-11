#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 VerseBox contributors
"""VerseBox ROM payload 规范化哈希：本仓库（Android 9）里**唯一**的实现。

规范来源：`docs/rom-package/02-ROM包结构与命名.md` §6.2。

把 `payload/` 下所有文件按「相对路径的 UTF-8 字节序」排序，对每个文件依次把

    相对路径(UTF-8) 、0x00 、字节数(十进制 ASCII) 、0x00 、文件字节

喂进同一个 SHA-256。**不**直接算 zip 的 sha256：那样会把压缩实现细节算进去，
同一个 payload 换一种压法就会假失败。

## 这份算法是**合同**，不是本仓库的口味

A9 / A10 / A15 是三个独立仓库，各自带一份同名实现；它们之间唯一必须逐字节一致的东西
就是这里的规范化算法——app 侧的解包器（`RomPackage`/`Roms` 的 L2 校验）只认这一种算法。
改这一份而不改 app 侧，会让"包能打出来、解包后自证失败"。核对方法：

    python rom/rom_hash.py <某个 payload 目录> --json   # 三域对同一个 payload 必须同值

用法：
    python rom/rom_hash.py payload/            # 打印哈希/文件数/字节数
    python rom/rom_hash.py payload/ --json     # 机器可读
"""
from __future__ import annotations

import hashlib
import json
import os
import sys

CHUNK = 1 << 20


def payload_entries(payload_root: str):
    """返回 [(相对路径(POSIX 分隔), 绝对路径)]，按相对路径的 UTF-8 字节序排序。"""
    entries = []
    for root, dirs, files in os.walk(payload_root):
        dirs.sort()
        for name in files:
            absolute = os.path.join(root, name)
            if os.path.islink(absolute):
                raise ValueError("payload 里不允许符号链接：%s" % absolute)
            rel = os.path.relpath(absolute, payload_root).replace(os.sep, "/")
            entries.append((rel, absolute))
    entries.sort(key=lambda item: item[0].encode("utf-8"))
    return entries


def payload_sha256(payload_root: str):
    """返回 (十六进制哈希, 文件数, 总字节数)。"""
    digest = hashlib.sha256()
    count = 0
    total = 0
    for rel, absolute in payload_entries(payload_root):
        size = os.path.getsize(absolute)
        digest.update(rel.encode("utf-8"))
        digest.update(b"\x00")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\x00")
        with open(absolute, "rb") as handle:
            while True:
                block = handle.read(CHUNK)
                if not block:
                    break
                digest.update(block)
        count += 1
        total += size
    return digest.hexdigest(), count, total


def file_sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        while True:
            block = handle.read(CHUNK)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    root = argv[1]
    if not os.path.isdir(root):
        print("不是目录：%s" % root)
        return 2
    hexdigest, count, total = payload_sha256(root)
    if "--json" in argv:
        print(json.dumps({"payloadSha256": hexdigest, "files": count, "bytes": total}))
    else:
        print("payloadSha256: %s" % hexdigest)
        print("files:         %d" % count)
        print("bytes:         %d" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
