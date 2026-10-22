#!/usr/bin/env python3
"""Extract the Redroid 9 layer into a regular-file tree plus a full index.

Windows cannot create the guest's device nodes and usually cannot create its
symlinks without elevation, so this script extracts regular files only and keeps
every entry's type, mode, owner and link target in rootfs-index.json. The A9
stager reconstructs symlinks and hard links in the APK manifest from that index.
"""
import json
import os
import posixpath
import stat
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAYER = ROOT / "downloads" / "9.0.0-240527-0.tar.gz"
TREE = ROOT / "rootfs"
INDEX = ROOT / "rootfs-index.json"

VIRTUAL = {"dev", "proc", "sys", "data", "mnt", "acct", "cache", "config", "d",
           "bugreports", "metadata", "storage", "sdcard", "tmp", "run", "lost+found"}


def normalize(name):
    name = name.replace("\\", "/")
    while name.startswith("./"):
        name = name[2:]
    name = name.lstrip("/")
    return posixpath.normpath(name) if name else ""


def allowed(path):
    if not path or path == ".":
        return True
    top = path.split("/", 1)[0]
    return top not in VIRTUAL


def long_path(path):
    absolute = str(path.resolve())
    if os.name == "nt" and not absolute.startswith("\\\\?\\"):
        return "\\\\?\\" + absolute
    return absolute


def main():
    if not LAYER.is_file():
        raise SystemExit("missing layer: " + str(LAYER))
    TREE.mkdir(parents=True, exist_ok=True)
    entries = {}
    hardlinks = []
    total = 0
    with tarfile.open(LAYER, "r:*") as archive:
        for member in archive:
            path = normalize(member.name)
            if not allowed(path):
                continue
            mode = member.mode & 0o7777
            record = {"path": path, "mode": mode, "uid": member.uid, "gid": member.gid,
                      "mtime": int(member.mtime)}
            if member.isdir():
                record["type"] = "dir"
                entries[path] = record
                (TREE / path).mkdir(parents=True, exist_ok=True)
                continue
            if member.islnk():
                record["type"] = "hardlink"
                record["target"] = normalize(member.linkname)
                entries[path] = record
                hardlinks.append(record)
                continue
            if member.issym():
                record["type"] = "symlink"
                record["target"] = member.linkname
                entries[path] = record
                continue
            if not member.isfile():
                record["type"] = "special"
                entries[path] = record
                continue
            record["type"] = "file"
            record["bytes"] = member.size
            entries[path] = record
            destination = TREE / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise SystemExit("unreadable member: " + path)
            with open(long_path(destination), "wb") as output:
                while True:
                    block = source.read(8 << 20)
                    if not block:
                        break
                    output.write(block)
            total += member.size
            if len(entries) % 500 == 0:
                print("  entries=%d files=%.1f MiB" % (len(entries), total / (1 << 20)), flush=True)
    for record in hardlinks:
        source = TREE / record["target"]
        destination = TREE / record["path"]
        if not source.is_file():
            record["type"] = "unresolved-hardlink"
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            destination.unlink()
        try:
            os.link(long_path(source), long_path(destination))
        except OSError:
            destination.write_bytes(source.read_bytes())
    INDEX.write_text(json.dumps({"layer": str(LAYER), "entries": entries}, indent=1) + "\n",
                     encoding="utf-8")
    counts = {}
    for record in entries.values():
        counts[record["type"]] = counts.get(record["type"], 0) + 1
    print(json.dumps({"entries": len(entries), "types": counts,
                      "bytes": sum(item.get("bytes", 0) for item in entries.values())}, indent=2))


if __name__ == "__main__":
    main()
