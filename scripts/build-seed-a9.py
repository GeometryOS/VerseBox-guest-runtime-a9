#!/usr/bin/env python3
"""Build an API 28 seed-properties tool from the pinned AOSP property sources.

The Android 10 tool refuses to seed a guest whose SDK is not 29, so Android 9
needs its own seed binary. Sources and the two container patches are the same
ones used by container-probe/properties; only the SDK gate and target change.
"""
import base64
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
A9 = ROOT / "guest-runtime-a9"
BUILD = A9 / "build" / "properties"
UPSTREAM = BUILD / "upstream"
DERIVED = BUILD / "derived"
OBJECTS = BUILD / "obj"
OUT = A9 / "artifacts" / "seed-properties-a9"
NDK = Path(r"C:/Users/22437/AppData/Local/Android/Sdk/ndk/29.0.14206865")
TOOL = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin"
CLANG = TOOL / "clang++.exe"
AR = TOOL / "llvm-ar.exe"
SYSROOT = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "sysroot"
LIBCXX = SYSROOT / "usr" / "lib" / "aarch64-linux-android" / "libc++_static.a"
LIBCXXABI = SYSROOT / "usr" / "lib" / "aarch64-linux-android" / "libc++abi.a"
BIONIC = "https://android.googlesource.com/platform/bionic/+/290c0cb5044b643e5d6cbcb1a5b275541ca3a89e"
CORE = "https://android.googlesource.com/platform/system/core/+/1dea9a052b7f214c10a77d5ed6ffd3602722a817"
SOURCES = ROOT / "container-probe" / "properties" / "sources.txt"
COMPILE = [
    "system_properties/context_node.cpp",
    "system_properties/contexts_serialized.cpp",
    "system_properties/prop_area.cpp",
    "system_properties/prop_info.cpp",
    "system_properties/system_properties.cpp",
    "async_safe/async_safe_log.cpp",
]
PARSER = "property_service/libpropertyinfoparser/property_info_parser.cpp"
ARCHIVE_FILES = ["context_node.o", "contexts_serialized.o", "prop_area.o", "prop_info.o",
                 "system_properties.o", "property_info_parser.o", "async_safe_log.o"]


def fetch(relative, base):
    destination = UPSTREAM / relative
    if destination.is_file():
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    url = base + "/" + relative.split("/", 1)[1] + "?format=TEXT"
    print("fetch", relative, flush=True)
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                destination.write_bytes(base64.b64decode(response.read()))
            break
        except Exception as failure:
            if attempt == 3:
                raise
            print("  retry", failure, flush=True)
    if not destination.stat().st_size:
        raise SystemExit("empty source: " + relative)


def main():
    for line in SOURCES.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        repository, relative = line.split(None, 1)
        fetch(repository + "/" + relative, BIONIC if repository == "bionic" else CORE)
    if DERIVED.exists():
        for path in (DERIVED / "bionic", DERIVED / "core"):
            if path.exists():
                import shutil
                shutil.rmtree(path)
    import shutil
    shutil.copytree(UPSTREAM / "bionic", DERIVED / "bionic")
    shutil.copytree(UPSTREAM / "core", DERIVED / "core")
    for patch in ("platform.patch", "runtime.patch"):
        subprocess.run(["git", "apply", "--recount", "--unsafe-paths",
                        "--directory=" + str(DERIVED),
                        str(ROOT / "container-probe" / "properties" / patch)], check=True)
    OBJECTS.mkdir(parents=True, exist_ok=True)
    B = DERIVED / "bionic" / "libc"
    C = DERIVED / "core" / "property_service" / "libpropertyinfoparser"
    include = ["-I" + str(B), "-I" + str(B / "include"),
               "-I" + str(B / "async_safe" / "include"),
               "-I" + str(B / "system_properties" / "include"),
               "-I" + str(C / "include")]
    common = [str(CLANG), "--target=aarch64-linux-android28", "--sysroot=" + str(SYSROOT),
              "-nostdinc++", "-isystem", str(SYSROOT / "usr" / "include" / "c++" / "v1"),
              "-std=c++17", "-O2", "-fPIC", "-fvisibility=hidden", "-fno-stack-protector",
              "-fno-exceptions", "-fno-rtti", "-ffunction-sections", "-fdata-sections",
              "-Wno-unused-parameter", "-Wno-vla-cxx-extension"] + include
    compiled = []
    for relative in COMPILE:
        object = OBJECTS / (Path(relative).stem + ".o")
        subprocess.run(common + ["-c", str(B / relative), "-o", str(object)], check=True)
        compiled.append(object)
    parser_object = OBJECTS / "property_info_parser.o"
    subprocess.run(common + ["-c", str(C / "property_info_parser.cpp"), "-o", str(parser_object)],
                   check=True)
    compiled.append(parser_object)
    archive = BUILD / "liba9vm-property-core.a"
    archive.unlink(missing_ok=True)
    subprocess.run([str(AR), "rcD", str(archive)] + [str(x) for x in compiled], check=True)

    seed = [str(CLANG), "--target=aarch64-linux-android28", "--sysroot=" + str(SYSROOT),
            "-nostdinc++", "-isystem", str(SYSROOT / "usr" / "include" / "c++" / "v1"),
            "-nostdlib++", "-std=c++17", "-O2", "-fPIE", "-pie", "-fvisibility=hidden",
            "-Wno-unused-parameter", "-Wno-vla-cxx-extension",
            "-I" + str(ROOT / "container-probe" / "properties" / "native")] + include + [
            str(A9 / "native" / "SeedPropertiesA9.cpp"),
            str(ROOT / "container-probe" / "properties" / "native" / "PrivateProperties.cpp"),
            str(ROOT / "container-probe" / "properties" / "native" / "PropertyInfo.cpp"),
            str(archive), str(LIBCXX), str(LIBCXXABI), "-ldl", "-lm",
            "-Wl,-z,max-page-size=4096", "-Wl,--build-id=sha1", "-o", str(OUT)]
    subprocess.run(seed, check=True)
    print("built", OUT, OUT.stat().st_size)


if __name__ == "__main__":
    main()
