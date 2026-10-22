#!/usr/bin/env python3
"""Report undefined symbols of key A9 binaries that no packaged library exports."""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
A9 = ROOT / "guest-runtime-a9"
NDK = Path(r"C:/Users/22437/AppData/Local/Android/Sdk/ndk/29.0.14206865")
READELF = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin" / "llvm-readelf.exe"

LINE = re.compile(r"^\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+\S+\s+\S+\s+\S+\s+(\S+)\s+(\S+)")


def symbols(path):
    result = subprocess.run([str(READELF), "--dyn-syms", "--wide", str(path)],
                            capture_output=True, text=True, errors="replace")
    defined, undefined = set(), set()
    for line in result.stdout.splitlines():
        match = LINE.match(line)
        if not match:
            continue
        section, name = match.group(1), match.group(2)
        base = name.split("@", 1)[0]
        if section == "UND":
            undefined.add(base)
        else:
            defined.add(base)
    return defined, undefined


def main():
    manifest = json.loads((A9 / "prepared" / "assets" / "guest-runtime-a9-inputs.json").read_text(encoding="utf-8"))
    libdir = A9 / "prepared" / "jniLibs" / "arm64-v8a"
    exports = set()
    for entry in manifest["files"]:
        try:
            defined, _ = symbols(libdir / entry["name"])
            exports |= defined
        except Exception:
            pass
    print("packaged exported symbols:", len(exports))
    focus = manifest["files"] if "--all" in sys.argv else [entry for entry in manifest["files"]
             if entry["logical"] in {
                 "libart.so", "libandroid_runtime.so", "libbinder.so", "libhwbinder.so",
                 "libhidlbase.so", "libutils.so", "libcutils.so", "libbase.so", "liblog.so",
                 "app_process64", "dalvikvm64", "surfaceflinger", "host-devices",
                 "libEGL_emulation.so", "libGLESv2_emulation.so", "gralloc.ranchu.so",
                 "hwcomposer.ranchu.so", "libOpenglSystemCommon.so", "servicemanager",
                 "hwservicemanager", "libc++.so", "liba10binder.so",
             }]
    failures = 0
    for entry in focus:
        defined, undefined = symbols(libdir / entry["name"])
        missing = sorted(name for name in undefined if name not in exports and not name.startswith("__cxa"))
        print("\n%s (%s): undefined=%d missing=%d" % (
            entry["logical"], entry["name"], len(undefined), len(missing)))
        for name in missing[:40]:
            print("  MISS", name)
        if missing:
            failures += 1
    print("\nfocus binaries with unresolved symbols:", failures)


if __name__ == "__main__":
    main()
