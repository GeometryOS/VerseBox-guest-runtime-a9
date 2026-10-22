#!/usr/bin/env python3
"""Audit the runtime symbol closure of one A9 ELF.

Resolves DT_NEEDED recursively using an explicit search path order, then
checks every versioned undefined symbol against the exported symbols of the
closure. Weak undefined symbols are reported but do not fail the audit: the
platform linker supplies those at load time.

Usage:
  python3 audit-closure.py --root <elf> --search <dir> [--search <dir> ...] \
      --output <json> [--nm <llvm-nm>] [--readelf <llvm-readelf>]
"""

import argparse
import json
import os
import re
import subprocess
import sys

DEFINED_RE = re.compile(r"^[0-9a-fA-F]+\s+([A-Za-z])\s+(\S+)$")
UNDEF_RE = re.compile(r"^\s*([UuWw])\s+(\S+)$")


def run(tool, args):
    proc = subprocess.run([tool] + args, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError("%s %s failed: %s" % (tool, " ".join(args), proc.stderr.strip()))
    return proc.stdout


def split_version(symbol):
    if "@@" in symbol:
        name, ver = symbol.split("@@", 1)
        return name, ver
    if "@" in symbol:
        name, ver = symbol.split("@", 1)
        return name, ver
    return symbol, None


def read_needed(readelf, path):
    out = run(readelf, ["-d", path])
    return re.findall(r"Shared library: \[([^\]]+)\]", out)


def read_defined(nm, path):
    out = run(nm, ["-D", "--defined-only", path])
    result = set()
    for line in out.splitlines():
        m = DEFINED_RE.match(line.strip())
        if m:
            result.add(m.group(2))
    return result


def read_undefined(nm, path):
    out = run(nm, ["-D", "-u", path])
    strong, weak = set(), set()
    for line in out.splitlines():
        m = UNDEF_RE.match(line.strip())
        if not m:
            continue
        kind, symbol = m.group(1), m.group(2)
        if kind == "U":
            strong.add(symbol)
        else:
            weak.add(symbol)
    return strong, weak


class Resolver:
    def __init__(self, search):
        self.search = search

    def find(self, soname):
        for directory in self.search:
            candidate = os.path.join(directory, soname)
            if os.path.isfile(candidate):
                return candidate
        return None


def match_symbol(undefined, exported):
    """Return True when `exported` satisfies `undefined`."""
    uname, uver = split_version(undefined)
    for candidate in exported:
        cname, cver = split_version(candidate)
        if cname != uname:
            continue
        if uver is None:
            return True
        if cver == uver:
            return True
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--search", action="append", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--nm", default="llvm-nm")
    parser.add_argument("--readelf", default="llvm-readelf")
    args = parser.parse_args()

    resolver = Resolver(args.search)
    loaded = {}
    queue = [(args.root, os.path.basename(args.root))]
    missing_files = []

    while queue:
        path, soname = queue.pop(0)
        if soname in loaded:
            continue
        loaded[soname] = path
        for needed in read_needed(args.readelf, path):
            if needed in loaded:
                continue
            found = resolver.find(needed)
            if found is None:
                missing_files.append({"soname": needed, "required_by": soname})
                continue
            queue.append((found, needed))

    exports = {}
    for soname, path in loaded.items():
        try:
            exports[soname] = read_defined(args.nm, path)
        except RuntimeError as exc:
            missing_files.append({"soname": soname, "error": str(exc)})
            exports[soname] = set()

    all_exports = set()
    for names in exports.values():
        all_exports |= names

    unresolved = []
    weak_unresolved = []
    for soname, path in loaded.items():
        try:
            strong, weak = read_undefined(args.nm, path)
        except RuntimeError:
            continue
        for symbol in sorted(strong):
            if not match_symbol(symbol, all_exports):
                unresolved.append({"library": soname, "symbol": symbol})
        for symbol in sorted(weak):
            if not match_symbol(symbol, all_exports):
                weak_unresolved.append({"library": soname, "symbol": symbol})

    report = {
        "root": args.root,
        "search": args.search,
        "libraries": sorted(loaded.keys()),
        "missing_files": missing_files,
        "unresolved_strong": unresolved,
        "unresolved_weak": weak_unresolved,
        "ok": not missing_files and not unresolved,
    }
    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, ensure_ascii=False)

    print("libraries: %d" % len(loaded))
    print("missing files: %d" % len(missing_files))
    print("unresolved strong: %d" % len(unresolved))
    print("unresolved weak: %d" % len(weak_unresolved))
    for item in missing_files[:10]:
        print("  MISSING %s (needed by %s)" % (item.get("soname"), item.get("required_by")))
    for item in unresolved[:20]:
        print("  UNRESOLVED %s in %s" % (item["symbol"], item["library"]))
    sys.exit(0 if report["ok"] else 1)


if __name__ == "__main__":
    main()
