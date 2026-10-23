#!/usr/bin/env python3
"""Build the Android 9 prepared runtime from the extracted Redroid 9 rootfs.

This mirrors the Android 10 four-stage pipeline, but reads a rootfs tree instead
of debugfs and uses the Android 9 layout (no APEX, /system framework, A9 HALs).
The source-built linker/libc/libdl/libm and the private Binder/device bridges are
reused from the audited Android 10 prepared runtime; every Android 9 system
component comes from the Redroid 9 image.
"""
import hashlib
import json
import os
import posixpath
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path

# This module is self-contained: MODULE is the repository root when this tree is
# cloned on its own, and is also the module directory inside the VM workspace.
MODULE = Path(__file__).resolve().parents[1]
A9 = MODULE
TREE = A9 / "rootfs"
INDEX = A9 / "rootfs-index.json"
A9_RUNTIME = A9 / "runtime" / "build"
A9_BRIDGES = A9 / "bridges"
# Optional: a copy of the Android 10 manifest, used only to build the hash
# denylist.  The A9 prepared never reads Android 10 binaries as inputs.
A10_MANIFEST = MODULE / "vendor" / "a10" / "guest-runtime-inputs.json"
OUT = A9 / "prepared"
ASSET_PREFIX = "guest-runtime-a9/"
NDK = Path(os.environ.get("ANDROID_NDK_HOME",
    r"C:/Users/22437/AppData/Local/Android/Sdk/ndk/29.0.14206865"))
READELF = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin" / "llvm-readelf.exe"
SDK = Path(os.environ.get("ANDROID_HOME", r"C:/Users/22437/AppData/Local/Android/Sdk"))
ANDROID_JAR = SDK / "platforms" / "android-35" / "android.jar"
D8 = SDK / "build-tools" / "35.0.0" / "d8.bat"

A9_32BIT_PREFIXES = ("system/lib/", "vendor/lib/", "odm/lib/", "oem/lib/",
                     "system/framework/arm/", "system/fake-libs/")
A9_32BIT_SUFFIXES = ("/oat/arm/",)

# Source-built API 28 runtime. logical, source, staged name, library name.
RUNTIME_FILES = [
    ("ld-android.so", A9_RUNTIME / "bin" / "source-linker64", "liba9vm-source-linker.so", "ld-android.so"),
    ("libc.so", A9_RUNTIME / "lib" / "libc.so", "liba9vm-guest-libc.so", "libc.so"),
    ("libdl.so", A9_RUNTIME / "lib" / "libdl.so", "liba9vm-guest-libdl.so", "libdl.so"),
    ("libm.so", A9_RUNTIME / "lib" / "libm.so", "liba9vm-guest-libm.so", "libm.so"),
]

# A9 arm64 keeps the ROM's self-contained C++ runtime: libc++.so already
# contains libc++abi and the LLVM unwinder, and libunwind.so is the GNU
# backtracer. Rebuilding these for arm64 is not what AOSP 9 does.
COMPANION_FILES = [
    ("libc++.so", TREE / "system" / "lib64" / "libc++.so", "liba9vm-guest-libc++.so", "libc++.so"),
    ("libunwind.so", TREE / "system" / "lib64" / "libunwind.so", "liba9vm-guest-libunwind.so", "libunwind.so"),
]

GRAPHICS_GUEST_PATHS = {
    "libEGL_emulation.so": "/vendor/lib64/egl/libEGL_emulation.so",
    "libGLESv1_CM_emulation.so": "/vendor/lib64/egl/libGLESv1_CM_emulation.so",
    "libGLESv2_emulation.so": "/vendor/lib64/egl/libGLESv2_emulation.so",
    "gralloc.ranchu.so": "/vendor/lib64/hw/gralloc.ranchu.so",
    "hwcomposer.ranchu.so": "/vendor/lib64/hw/hwcomposer.ranchu.so",
}

# Project bridges rebuilt against the API 28 runtime.
BRIDGE_FILES = [
    ("liba9vm-property-client.so", A9_BRIDGES / "lib" / "liba9vm-property-client.so",
     "liba9vm-property-client.so", "liba9vm-property-client.so", None),
    ("liba9binder.so", A9_BRIDGES / "lib" / "liba9binder.so",
     "liba9binder.so", "liba9binder.so", None),
    ("liba9vm-binder-devices.so", A9_BRIDGES / "lib" / "liba9vm-binder-devices.so",
     "liba9vm-binder-devices.so", "liba9vm-binder-devices.so", None),
    ("liba9vm-guest-devices.so", A9_BRIDGES / "lib" / "liba9vm-guest-devices.so",
     "liba9vm-guest-devices.so", "liba9vm-guest-devices.so", None),
    ("liba9vm-guest-log.so", A9_BRIDGES / "lib" / "liba9vm-guest-log.so",
     "liba9vm-guest-log.so", "liba9vm-guest-log.so", None),
    ("liba9vm-manager-policy.so", A9_BRIDGES / "lib" / "liba9vm-manager-policy.so",
     "liba9vm-manager-policy.so", "liba9vm-manager-policy.so", None),
    # Redroid 9 ships only a 32-bit dex2oat, so every installd DexInv fails and
    # PackageManagerService retries it for all system packages on every boot
    # (measured: ~74 failures per cold boot, tens of seconds of stall).  The
    # prepared image therefore carries a 64-bit static-PIE /system/bin/dex2oat
    # stub (see dex2oat-stub.S).  This shim stays as the second line of defence
    # for any installd path that calls execv/execve on the original name.
    ("liba9vm-dexopt-shim.so", A9_BRIDGES / "lib" / "liba9vm-dexopt-shim.so",
     "liba9vm-dexopt-shim.so", "liba9vm-dexopt-shim.so", None),
    # API 28 healthd reads /sys/class/power_supply, which the guest path layer
    # leaves on the host sysfs.  The wrapper redirects only that prefix to the
    # per-instance battery-sysfs directory the host keeps current (Android 10
    # uses its health HAL instead).
    ("liba9vm-healthd-sysfs.so", A9_BRIDGES / "lib" / "liba9vm-healthd-sysfs.so",
     "liba9vm-healthd-sysfs.so", "liba9vm-healthd-sysfs.so", None),
    ("libOpenglSystemCommon.so", A9_BRIDGES / "graphics" / "lib" / "libOpenglSystemCommon.so",
     "liba9vm-guest-libOpenglSystemCommon.so", "libOpenglSystemCommon.so", None),
    ("libEGL_emulation.so", A9_BRIDGES / "graphics" / "lib" / "libEGL_emulation.so",
     "liba9vm-guest-libEGL_emulation.so", "libEGL_emulation.so",
     GRAPHICS_GUEST_PATHS["libEGL_emulation.so"]),
    ("libGLESv1_CM_emulation.so", A9_BRIDGES / "graphics" / "lib" / "libGLESv1_CM_emulation.so",
     "liba9vm-guest-libGLESv1_CM_emulation.so", "libGLESv1_CM_emulation.so",
     GRAPHICS_GUEST_PATHS["libGLESv1_CM_emulation.so"]),
    ("libGLESv2_emulation.so", A9_BRIDGES / "graphics" / "lib" / "libGLESv2_emulation.so",
     "liba9vm-guest-libGLESv2_emulation.so", "libGLESv2_emulation.so",
     GRAPHICS_GUEST_PATHS["libGLESv2_emulation.so"]),
    ("gralloc.ranchu.so", A9_BRIDGES / "graphics" / "lib" / "gralloc.ranchu.so",
     "liba9vm-guest-gralloc.ranchu.so", "gralloc.ranchu.so",
     GRAPHICS_GUEST_PATHS["gralloc.ranchu.so"]),
    ("hwcomposer.ranchu.so", A9_BRIDGES / "graphics" / "lib" / "hwcomposer.ranchu.so",
     "liba9vm-guest-hwcomposer.ranchu.so", "hwcomposer.ranchu.so",
     GRAPHICS_GUEST_PATHS["hwcomposer.ranchu.so"]),
    ("liba9vm-hal-modules.so", A9_BRIDGES / "graphics" / "lib" / "liba9vm-hal-modules.so",
     "liba9vm-hal-modules.so", "liba9vm-hal-modules.so", None),
]

# Standalone helper programs, staged as jniLibs entries and linked into the
# guest rootfs by logical name.
PROGRAM_FILES = [
    ("property-service", A9_BRIDGES / "bin" / "property-service",
     "liba9vm-property-service.so"),
    ("seed-properties", A9_BRIDGES / "bin" / "seed-properties",
     "liba9vm-seed-properties.so"),
    ("binder-broker", A9_BRIDGES / "bin" / "binder-broker",
     "liba9vm-binder-broker.so"),
    ("host-devices", A9_BRIDGES / "bin" / "host-devices",
     "liba9vm-host-devices.so"),
    # API 28's netd endpoint is a socket, not a process: NetworkManagementService
    # waits for a connectable /dev/socket/netd and NetdService resolves the binder
    # "netd" service.  Running the ROM netd binary needs CAP_NET_ADMIN, which a
    # container does not have, so the socket end is our own listener.
    ("netd-listen", A9_BRIDGES / "bin" / "netd-listen", "liba9vm-netd-listen.so"),
]

PLATFORM_LIBRARIES = {
    "propertyClient": "liba9vm-property-client.so",
    "guestDevices": "liba9vm-guest-devices.so",
    "binderDevices": "liba9vm-binder-devices.so",
    "guestLog": "liba9vm-guest-log.so",
    "managerPolicy": "liba9vm-manager-policy.so",
    "binderCore": "liba9binder.so",
    "halModules": "liba9vm-hal-modules.so",
}

A10_LOGICAL_PREFIXES = ("liba10vm-", "liba10")
A10_DOMAIN_LOGICALS = {
    "ld-android.so", "libc.so", "libdl.so", "libm.so", "libunwind.so", "libc++.so",
    "liba10binder.so", "liba10vm-property-client.so", "liba10vm-guest-devices.so",
    "liba10vm-binder-devices.so", "liba10vm-manager-policy.so", "liba10vm-guest-log.so",
    "libOpenglSystemCommon.so", "libEGL_emulation.so", "libGLESv1_CM_emulation.so",
    "libGLESv2_emulation.so", "gralloc.ranchu.so", "hwcomposer.ranchu.so",
}
SHIM_LOGICALS = {"liba9vm-malloc-leak-shim.so", "liba9vm-libdl-compat.so"}

A9_PROGRAMS = {
    "dalvikvm64": "system/bin/dalvikvm64",
    "app_process64": "system/bin/app_process64",
    "servicemanager": "system/bin/servicemanager",
    "hwservicemanager": "system/bin/hwservicemanager",
    "surfaceflinger": "system/bin/surfaceflinger",
    "health": "system/bin/healthd",
    "installd": "system/bin/installd",
    "keystore": "system/bin/keystore",
    "graphics-composer": "vendor/bin/hw/android.hardware.graphics.composer@2.1-service",
    "graphics-allocator": "vendor/bin/hw/android.hardware.graphics.allocator@2.0-service",
    "graphics-configstore": "vendor/bin/hw/android.hardware.configstore@1.1-service",
    "audio-hal": "vendor/bin/hw/android.hardware.audio@2.0-service",
    "audioserver": "system/bin/audioserver",
    "media": "system/bin/mediaserver",
    "media-omx": "vendor/bin/hw/android.hardware.media.omx@1.0-service",
    "drm": "vendor/bin/hw/android.hardware.drm@1.0-service",
    "gatekeeper": "vendor/bin/hw/android.hardware.gatekeeper@1.0-service",
    "keymaster": "vendor/bin/hw/android.hardware.keymaster@3.0-service",
}

PROPERTY_SOURCES = [
    "/system/etc/prop.default", "/vendor/default.prop", "/system/build.prop",
    "/vendor/build.prop", "/odm/etc/build.prop", "/odm/build.prop",
    "/system/product/build.prop",
]


def digest(path):
    value = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(4 << 20), b""):
            value.update(block)
    return value.hexdigest()


def readelf_needed(path):
    result = subprocess.run([str(READELF), "-d", str(path)], capture_output=True, text=True,
                            errors="replace")
    return re.findall(r"\(NEEDED\).*?\[(.*?)\]", result.stdout)


def elf_kind(path):
    with open(path, "rb") as stream:
        header = stream.read(20)
    if header[:6] == b"\x7fELF\x02\x01" and header[18:20] == b"\xb7\x00":
        return "arm64"
    if header[:4] == b"\x7fELF":
        return "other"
    return None


def sanitize(name):
    return name.replace("/", "-").replace("@", "_")


def excluded(path):
    if any(path.startswith(prefix) for prefix in A9_32BIT_PREFIXES):
        return True
    return any(suffix in "/" + path for suffix in A9_32BIT_SUFFIXES)


def stage_copy(source, folder, name):
    destination = OUT / folder / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    return destination


def main():
    skip_dex = "--skip-dex" in sys.argv
    index = json.loads(INDEX.read_text(encoding="utf-8"))["entries"]

    for logical, guest in A9_PROGRAMS.items():
        if guest not in index or index[guest]["type"] != "file":
            raise SystemExit("Missing Android 9 program %s at /%s" % (logical, guest))

    if OUT.exists():
        shutil.rmtree(OUT)
    (OUT / "assets").mkdir(parents=True)
    native = {}
    data = {}
    native_links = []
    used_guest_paths = set()
    library_names = set()
    files = []
    programs_named = set()
    OVERRIDE_GUEST_PATHS = {
        "system/bin/linker64", "system/lib64/ld-android.so", "system/lib64/libc.so",
        "system/lib64/libdl.so", "system/lib64/libm.so", "system/lib64/libunwind.so",
        "system/lib64/libc++.so",
    }
    used_guest_paths.update(OVERRIDE_GUEST_PATHS)

    def add_native(source, logical, name, source_kind, library_name=None, guest_path=None,
                   mode=0o755):
        if logical in native:
            return native[logical]
        source = Path(source)
        if not source.is_file():
            raise SystemExit(
                "Missing A9 build output for %s: %s\n"
                "Build the matching phase first; see docs/android9-port/06 and 07." % (logical, source))
        destination = stage_copy(source, "jniLibs/arm64-v8a", name)
        try:
            entry_source = source.relative_to(MODULE).as_posix()
        except ValueError:
            entry_source = source.as_posix()
        entry = {"name": name, "logical": logical, "bytes": destination.stat().st_size,
                 "sha256": digest(destination), "mode": mode, "source": source_kind,
                 "sourcePath": entry_source}
        if library_name:
            entry["libraryName"] = library_name
        if guest_path:
            entry["guestPath"] = guest_path
        files.append(entry)
        native[logical] = entry
        if library_name:
            library_names.add(library_name)
        return entry

    # 1. Source-built API 28 runtime, ROM companion, project bridges.
    for logical, path, name, library in RUNTIME_FILES:
        add_native(path, logical, name, "aosp9", library)
    native_links.append({"path": "rootfs/system/bin/linker64", "logical": "ld-android.so"})
    for logical, path, name, library in COMPANION_FILES:
        add_native(path, logical, name, "redroid9", library)
    missing = []
    for logical, path, name, library, guest_path in BRIDGE_FILES:
        if not path.is_file():
            missing.append(logical)
            continue
        add_native(path, logical, name, "project-a9", library, guest_path)
    for logical, path, name in PROGRAM_FILES:
        if not path.is_file():
            missing.append(logical)
            continue
        add_native(path, logical, name, "project-a9")
    if missing:
        print("prepare-a9: not built yet, skipped: " + ", ".join(sorted(missing)),
              file=sys.stderr)

    # 2. Android 9 system programs launched by FrameworkSession.
    for logical, guest in A9_PROGRAMS.items():
        add_native(TREE / guest, logical, "liba9vm-" + sanitize(logical) + ".so", "redroid9")
        native_links.append({"path": "rootfs/" + guest, "logical": logical})
        used_guest_paths.add(guest)
        programs_named.add(guest)

    # 3. A9 native system libraries: system/lib64 wins, then vendor/unique names.
    arm64 = []
    for path, record in index.items():
        if record["type"] != "file" or excluded(path):
            continue
        kind = elf_kind(TREE / path)
        if kind == "arm64":
            arm64.append((path, record))
    arm64.sort(key=lambda item: (not item[0].startswith("system/lib64/"),
                                 item[0].count("/"), item[0]))
    for path, record in arm64:
        if path in used_guest_paths:
            continue
        base = posixpath.basename(path)
        logical = None
        guest_name = "liba9vm-image-" + hashlib.sha256(path.encode()).hexdigest()[:20] + ".so"
        if path.startswith("system/lib64/"):
            rest = path[len("system/lib64/"):]
            depth = rest.count("/")
            # /system/lib64 wins; VNDK directories only fill names the root does
            # not provide (A9 keeps libpuresoftkeymasterdevice.so in vndk-28 only,
            # and keymaster@3.0-impl.so cannot be loaded without it).
            if (depth == 0 or (depth == 1 and rest.startswith("vndk-"))) \
                    and base not in library_names and base not in native:
                logical = base
        if logical is None:
            logical = "image/" + path
        entry = add_native(TREE / path, logical, guest_name, "redroid9",
                           base if logical == base and base.endswith(".so") else None)
        native_links.append({"path": "rootfs/" + path, "logical": logical})
        used_guest_paths.add(path)
        # Redroid 9 ships android.hardware.graphics.mapper@2.0-impl.so but omits the
        # "-2.1" variant that IMapper::getService() tries first.  ComposerHal then
        # reports "failed to get mapper service" and SurfaceFlinger cannot create a
        # composer client.  The packaged library already exports HIDL_FETCH_IMapper,
        # so expose the same bytes under the missing name instead of rebuilding
        # AOSP's mapper backend (which would need the full hardware/interfaces tree).
        if path == "vendor/lib64/hw/android.hardware.graphics.mapper@2.0-impl.so":
            for name in ("android.hardware.graphics.mapper@2.0-impl.so",
                         "android.hardware.graphics.mapper@2.0-impl-2.1.so"):
                native_links.append({"path": "rootfs/vendor/lib64/hw/" + name, "logical": logical})
                # /system/lib64 is the only directory the ROM's own
                # ld.config.28.txt searches for the default namespace, so expose the
                # mapper there too: HIDL passthrough loads it by soname.
                native_links.append({"path": "rootfs/system/lib64/" + name, "logical": logical})

    # 4. Registry helper closure. Android 9 helpers share the default API 28
    # namespace, so no isolated registry library map is needed.
    registry = {}

    # 5. Remaining immutable files become assets. Windows and AAPT are
    # case-insensitive, so names that differ only by case are stored under a
    # hashed asset name and restored through installPath.
    candidates = []
    for path, record in index.items():
        if record["type"] != "file" or excluded(path):
            continue
        if path in used_guest_paths or elf_kind(TREE / path) == "arm64":
            continue
        candidates.append(path)
    lower_counts = {}
    for path in candidates:
        normal = ASSET_PREFIX + "rootfs/" + path
        key = normal.lower()
        lower_counts[key] = lower_counts.get(key, 0) + 1
    dex2oat_stub = A9_BRIDGES / "bin" / "dex2oat-stub"
    if not dex2oat_stub.is_file():
        print("prepare-a9: dex2oat-stub missing; packaging the ROM's 32-bit dex2oat",
              file=sys.stderr)
    for path in candidates:
        record = index[path]
        normal = ASSET_PREFIX + "rootfs/" + path
        special = normal.endswith((".bc", ".gz"))
        encoded = special or lower_counts[normal.lower()] > 1
        if encoded:
            asset = ASSET_PREFIX + "rom-encoded/" + hashlib.sha256(path.encode()).hexdigest() + ".asset"
        else:
            asset = normal
        # The image's /system/bin/dex2oat is ELF32/EM_ARM.  The API 28 runtime is
        # 64-bit, so every installd DexInv fails with 0x001f and PackageManager
        # retries all system packages on every boot.  Package a 64-bit static
        # PIE stub instead: whichever exec path installd uses, the child loads
        # and exits 0, so the DexInv succeeds without a doomed 32-bit exec.
        # No oat is produced; the guest keeps running interpreter + JIT, which
        # is what it already does.
        source = TREE / path
        source_kind = "redroid9"
        if path == "system/bin/dex2oat" and dex2oat_stub.is_file():
            source = dex2oat_stub
            source_kind = "project-a9"
        destination = stage_copy(source, "assets", asset)
        entry = {"name": asset, "bytes": destination.stat().st_size, "sha256": digest(destination),
                 "source": source_kind, "mode": stat.S_IMODE(record["mode"])}
        if encoded:
            entry["installPath"] = "rootfs/" + path
        if asset in data:
            raise SystemExit("Duplicate asset: " + asset)
        data[asset] = entry

    # 6. Symlinks and directories.
    symlinks = []
    directories = set()
    for path, record in index.items():
        if excluded(path):
            continue
        if record["type"] == "dir":
            directories.add(path)
        elif record["type"] == "symlink":
            target = record["target"]
            resolved = target if target.startswith("/") else posixpath.normpath(
                posixpath.join(posixpath.dirname(path), target))
            if excluded(resolved.lstrip("/")):
                continue
            symlinks.append({"path": "rootfs/" + path, "target": target})
    links = []
    for path, record in index.items():
        if record["type"] == "hardlink" and not excluded(path):
            links.append({"path": "rootfs/" + path, "target": "rootfs/" + record["target"]})

    # 7. Boot classpath and system server classpath from the Android 9 init files.
    environ = (TREE / "init.environ.rc").read_text(encoding="utf-8", errors="replace")
    boot = re.search(r"^\s*export BOOTCLASSPATH (.+)$", environ, re.M).group(1).split(":")
    system_server = re.search(r"^\s*export SYSTEMSERVERCLASSPATH (.+)$", environ, re.M).group(1).split(":")
    boot = [item for item in boot if (TREE / item.lstrip("/")).is_file()]
    system_server = [item for item in system_server if (TREE / item.lstrip("/")).is_file()]
    art_boot = ["/system/framework/core-oj.jar", "/system/framework/core-libart.jar"]

    # 8. Properties: ROM precedence plus the A9 container overlay.
    properties = {}
    property_files = []
    for source in PROPERTY_SOURCES:
        path = TREE / source.lstrip("/")
        if not path.is_file():
            continue
        property_files.append(source)
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            name, separator, value = line.partition("=")
            if separator and re.fullmatch(r"[A-Za-z0-9_.-]+", name.strip()):
                properties[name.strip()] = value.strip()
    overlay = A9 / "guest-a9.prop"
    for line in overlay.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, _, value = line.partition("=")
        properties[name.strip()] = value.strip()
    order = properties.get("ro.product.property_source_order", "product,product_services,odm,vendor,system").split(",")
    for field in ("brand", "device", "manufacturer", "model", "name"):
        name = "ro.product." + field
        if properties.get(name):
            continue
        for partition in order:
            value = properties.get("ro.product." + partition + "." + field)
            if value:
                properties[name] = value
                break
    properties.setdefault("ro.build.fingerprint", properties.get("ro.build.fingerprint", "redroid/redroid_arm64/redroid_arm64:9/PI/frank05271342:userdebug/test-keys"))
    guest_prop = "".join(name + "=" + value + "\n" for name, value in sorted(properties.items()))
    prop_asset = ASSET_PREFIX + "guest.prop"
    prop_file = OUT / "assets" / prop_asset
    prop_file.parent.mkdir(parents=True, exist_ok=True)
    prop_file.write_text(guest_prop, encoding="utf-8", newline="\n")
    data[prop_asset] = {"name": prop_asset, "bytes": prop_file.stat().st_size,
                        "sha256": digest(prop_file), "mode": 0o644}

    # 9. Host device VINTF entry.
    host_vintf = A9 / "vintf" / "host-devices.xml"
    vintf_asset = ASSET_PREFIX + "rootfs/vendor/etc/vintf/manifest/a10vm-host-devices.xml"
    vintf_file = OUT / "assets" / vintf_asset
    vintf_file.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(host_vintf, vintf_file)
    data[vintf_asset] = {"name": vintf_asset, "bytes": vintf_file.stat().st_size,
                         "sha256": digest(vintf_file), "mode": 0o644}
    directories.add("vendor/etc/vintf/manifest")

    # 10. Desktop DEX files.
    if not skip_dex:
        build_dex()
        for asset in ("framework-check.dex", "runtime-check.dex"):
            meta = json.loads((A9 / "artifacts" / (asset + ".json")).read_text(encoding="utf-8"))
            data[meta["asset"]] = {"name": meta["asset"], "bytes": meta["bytes"],
                                   "sha256": meta["sha256"], "mode": 0o644}

    # 11. System packages.
    packages = {}
    for directory in ("system/app", "system/priv-app", "system/product/app", "system/product/priv-app",
                      "system/system_ext/app", "system/system_ext/priv-app", "vendor/app", "vendor/priv-app"):
        if not (TREE / directory).is_dir():
            continue
        for root, _, names in os.walk(TREE / directory):
            for name in names:
                if not name.endswith(".apk"):
                    continue
                absolute = Path(root) / name
                relative = absolute.relative_to(TREE).as_posix()
                packages[relative] = "/" + relative
    named = {
        "framework": "system/framework/framework-res.apk",
        "launcher": "system/priv-app/Launcher3QuickStep/Launcher3QuickStep.apk",
        "systemui": "system/priv-app/SystemUI/SystemUI.apk",
        "settingsProvider": "system/priv-app/SettingsProvider/SettingsProvider.apk",
        "settings": "system/priv-app/Settings/Settings.apk",
        "packageInstaller": "system/priv-app/PackageInstaller/PackageInstaller.apk",
        "shell": "system/priv-app/Shell/Shell.apk",
        "extServices": "system/priv-app/ExtServices/ExtServices.apk",
        "extShared": "system/app/ExtShared/ExtShared.apk",
        "mediaProvider": "system/priv-app/MediaProvider/MediaProvider.apk",
        "documents": "system/priv-app/DocumentsUI/DocumentsUI.apk",
        "externalStorageProvider": "system/priv-app/ExternalStorageProvider/ExternalStorageProvider.apk",
        "downloadProvider": "system/priv-app/DownloadProvider/DownloadProvider.apk",
        "fusedLocation": "system/priv-app/FusedLocation/FusedLocation.apk",
        "gallery": "system/app/Gallery2/Gallery2.apk",
        "keyboard": "system/app/LatinIME/LatinIME.apk",
    }
    for key, relative in named.items():
        if relative in packages:
            packages[key] = "/" + relative

    # 12. Manifest.
    initial = set(directories)
    initial.update({"system/bin", "system/lib64", "vendor/bin/hw", "vendor/lib64/hw",
                    # Directories an Android 9 init creates before framework boot.
                    # /cache is not optional: StatsCompanionService constructs
                    # StatFs("/cache") in its constructor and the whole system
                    # server dies with "Invalid path: /cache" when it is absent.
                    "cache", "acct", "config", "metadata", "lost+found",
                    "storage/emulated/0", "data/misc/stats-service",
                    "vendor/lib64/egl", "dev/input", "data/local/tmp", "data/dalvik-cache",
                    "data/dalvik-cache/arm64", "data/anr", "data/tombstones", "data/misc/user",
                    "data/misc/profiles/cur", "data/misc/profiles/ref", "data/misc/shared_relro",
                    "data/misc/media", "data/misc/mediadrm", "data/mediadrm", "data/vendor/mediadrm",
                    "data/mediaserver", "data/drm", "data/misc/audioserver", "data/system",
                    "data/system/users", "data/user", "data/user_de", "data/app", "data/app-lib",
                    "data/app-private", "data/app-ephemeral", "data/app-staging", "mnt/asec",
                    "mnt/expand", "data/user/0", "data/user_de/0", "data/system/users/0",
                    "data/misc/profiles/cur/0", "data/misc/keystore", "data/misc/keymaster",
                    # The framework's scheduler groups live in cpuctl on API 28.
                    # There is no cgroup mount in a container, so the guest needs
                    # the nodes an Android init would have created; GuestRuntime
                    # materialises the tasks files themselves.
                    "dev/cpuctl/top-app", "dev/cpuctl/foreground", "dev/cpuctl/background"})
    profile = {
        "graphics": True,
        "systemSuspend": False,
        "idmap2d": False,
        "health": True,
        "keystore": True,
        "audio": False,
        "media": False,
        "installer": True,
        # The image has no 64-bit dex2oat, so AOT is impossible here; skip the
        # doomed installd retry instead of stalling every cold boot.
        "dexoptShim": True,
        "camera": False,
        "halModules": True,
        "registryHelpers": False,
        "dexCache": False,
        "appClass": "io.a10vm.guest.A9GuestApplication",
        "keymasterLogical": "keymaster",
        "keystoreLogical": "keystore",
        # Unused: FrameworkSession reads the top-level extraPreloads.  Kept
        # empty so nobody re-adds the shim here by mistake.
        "extraPreloads": [],
        "bootEntry": "io.a10vm.guest.A9Boot",
        "checkClass": "io.a10vm.guest.A9DesktopCheck",
        "inputClass": "io.a10vm.guest.DesktopInput",
        "readyMarker": "desktop_services_ready=1",
        "artStartupSeconds": 60,
        "services": [
            # API 28's NetworkManagementService.create() blocks on a CountDownLatch
            # until NativeDaemonConnector connects to /dev/socket/netd, and its
            # NetdService resolves the binder service "netd" registered by
            # GuestNetworks from the bootstrap dex.  This listener only owns the
            # socket end; the binder end lives in A9Boot.
            {"role": "netd", "logical": "netd-listen", "uid": "0", "gid": "0",
             "preload": "liba9vm-guest-log.so", "arguments": ["dev/socket/netd"]},
            # API 28's NsdService.create() blocks on the same pattern for
            # /dev/socket/mdns (NsdService.java:657).  Android 10 provides this
            # endpoint from GuestDiscovery; the API 28 runtime reuses the socket
            # listener program for it.
            {"role": "mdns", "logical": "netd-listen", "uid": "0", "gid": "0",
             "preload": "liba9vm-guest-log.so", "arguments": ["dev/socket/mdns"]},
            # The Android 9 gatekeeper HIDL service is deliberately not started.
            # Its HIDL_FETCH_IGatekeeper() calls hw_get_module_by_class("gatekeeper",
            # "default"), but Redroid 9 only ships hw/gatekeeper.redroid.so, so the
            # service logs "Unable to open GateKeeper HAL" and aborts with SIGABRT
            # (code 134).  Android 10 does not start it either, and this guest's
            # keystore already runs on the software keymaster
            # ("found IKeymasterDevice ... seclevel SOFTWARE").  Because
            # FrameworkSession.error() reports any exited child, that abort used to
            # tear down a fully booted desktop ~2 s after it became RUNNING.
        ],
    }
    manifest = {
        "guestSdk": 28,
        "frameworkSdk": 28,
        "loaderFromSource": True,
        "rootRequired": False,
        "artIntegrated": True,
        "frameworkIntegrated": True,
        "desktopImplemented": True,
        "apexAliases": False,
        "assetPrefix": ASSET_PREFIX,
        "runtimeRoot": "/system",
        "tzdataRoot": "/system",
        "vintfAllManifests": False,
        "hostDevicesManifest": "vendor/etc/vintf/manifest/a10vm-host-devices.xml",
        "bootClassPath": boot,
        "artBootClassPath": art_boot,
        "systemServerClassPath": system_server,
        "platformLibraries": PLATFORM_LIBRARIES,
        "registryLibraries": registry,
        "desktopPackages": packages,
        "allSystemPackages": True,
        "systemApkCount": len(packages),
        # True when /system/bin/dex2oat in this prepared tree is the project's
        # 64-bit static PIE stub instead of the image's ELF32/EM_ARM binary.
        "dex2oatStub": dex2oat_stub.is_file(),
        "propertyFiles": property_files,
        "propertyCount": len(properties),
        # FrameworkSession preloads these into every guest process through the
        # top-level manifest field.  It must stay at the top level: the old
        # runtimeProfile copy was never read, so healthd never saw the shim.
        "extraPreloads": ["liba9vm-healthd-sysfs.so"],
        "runtimeProfile": profile,
        "symlinks": symlinks,
        "links": links,
        "nativeLinks": native_links,
        "systemImagePaths": len(index),
        "mutableDataPreserved": True,
        "initialDirectories": sorted(initial),
        "sourceImageSha256": digest(A9 / "downloads" / "9.0.0-240527-0.tar.gz"),
        "files": files,
        "data": list(data.values()),
    }
    # Isolation gate: judge by provenance and content hash, never by a logical
    # name alone. A9's own libc.so and libdl.so are legitimate.
    a10_hashes = set()
    if A10_MANIFEST.is_file():
        a10 = json.loads(A10_MANIFEST.read_text(encoding="utf-8"))
        for entry in a10.get("files", []):
            logical = entry.get("logical", "")
            if logical in A10_DOMAIN_LOGICALS or logical.startswith(A10_LOGICAL_PREFIXES):
                a10_hashes.add(entry["sha256"])
    violations = []
    for entry in files:
        logical = entry["logical"]
        if entry.get("source") not in {"redroid9", "aosp9", "project-a9"}:
            violations.append("unknown source %r: %s" % (entry.get("source"), logical))
        if entry.get("sha256") in a10_hashes:
            violations.append("Android 10 binary content: %s (%s)" % (logical, entry["sourcePath"]))
        if logical.startswith(A10_LOGICAL_PREFIXES):
            violations.append("Android 10 logical name: %s" % logical)
        if logical in SHIM_LOGICALS:
            violations.append("shim is not allowed: %s" % logical)
    if violations:
        raise SystemExit(
            "A9 isolation gate failed:\n  " + "\n  ".join(sorted(set(violations)))
            + "\nBuild the API 28 runtime and bridges first; see docs/android9-port/06 and 07.")

    sources = {}
    for entry in files:
        bucket = sources.setdefault(entry["source"], {"count": 0, "files": {}})
        bucket["count"] += 1
        bucket["files"][entry["logical"]] = entry["sha256"]
    (OUT / "sources.json").write_text(
        json.dumps(sources, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")

    manifest_file = OUT / "assets" / "guest-runtime-a9-inputs.json"
    manifest_file.write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"nativeFiles": len(files), "dataFiles": len(data), "symlinks": len(symlinks),
                      "registryLibraries": len(registry), "prepared": str(OUT),
                      "sources": {key: value["count"] for key, value in sources.items()}}, indent=2))


def build_dex():
    classes = A9 / "artifacts" / "a9-classes"
    runtime_classes = A9 / "artifacts" / "a9-runtime-classes"
    for directory in (classes, runtime_classes):
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
    framework_sources = [
        A9 / "bootstrap" / "io" / "a10vm" / "guest" / "A9Boot.java",
        A9 / "bootstrap" / "io" / "a10vm" / "guest" / "A9DesktopCheck.java",
        A9 / "bootstrap" / "io" / "a10vm" / "guest" / "A9DesktopHome.java",
        A9 / "bootstrap" / "io" / "a10vm" / "guest" / "A9GuestApplication.java",
        A9 / "bootstrap" / "io" / "a10vm" / "guest" / "A9AudioPolicy.java",
        # API 28 media services are published by the 32-bit mediaserver, which
        # cannot run here.  No-op endpoints keep ServiceManager lookups from
        # blocking the app UI thread for 5 s each.
        A9 / "bootstrap" / "io" / "a10vm" / "guest" / "A9MediaPolicy.java",
        # Registers the no-op gatekeeper endpoint LockSettingsService needs on
        # the first provision (user_setup_complete); without it system_server
        # dies with a NullPointerException before the first desktop appears.
        A9 / "bootstrap" / "io" / "a10vm" / "guest" / "A9GateKeeper.java",
    ]
    for name in ("ProcessEvidence", "DesktopInput", "DesktopProcesses", "GuestMemory", "GuestScheduling",
                 # GuestDisplayPower (shared with Android 10) launches
                 # io.a10vm.guest.GuestDisplayState; without it every
                 # updateDisplayPower() tick throws ClassNotFoundException.
                 "GuestDisplayState",
                 "PrivateVolumes", "LocalGuestService", "GuestCapabilities", "GuestSetup",
                 "GuestInputMethods", "GuestHardwareApplications", "GuestWebViewProviders",
                 "DesktopHome", "GuestCommand", "GuestInstall", "GuestMediaScan",
                 "AppLaunch", "AppRuntimeOptions", "GuestWebView", "AppDexCache",
                 # A9Boot registers the API 28 netd endpoint through GuestNetworks.
                 # The class itself only needs GuestConnectivity for its snapshots;
                 # the resolver/wifi/discovery helpers it references are compiled
                 # for linkage but never started on API 28.
                 "GuestNetworks", "GuestConnectivity", "GuestWifi", "GuestWifiChannel",
                 "GuestDiscovery", "GuestDnsResolver", "GuestOemNetd"):
        framework_sources.append(MODULE / "vendor" / "framework" / (name + ".java"))
    compile_java(framework_sources, classes)
    d8(classes, A9 / "artifacts" / "a9-dex")
    runtime_sources = [MODULE / "vendor" / "framework" / name for name in
                       ("RuntimeCheck.java", "DirectoryCheck.java", "ProcessEvidence.java")]
    compile_java(runtime_sources, runtime_classes)
    d8(runtime_classes, A9 / "artifacts" / "a9-runtime-dex")
    for source, asset in ((A9 / "artifacts" / "a9-dex" / "classes.dex", "framework-check.dex"),
                          (A9 / "artifacts" / "a9-runtime-dex" / "classes.dex", "runtime-check.dex")):
        destination = OUT / "assets" / ASSET_PREFIX / asset
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        # The caller appends them to the data list after staging; keep a marker file.
        (A9 / "artifacts" / (asset + ".json")).write_text(json.dumps({
            "asset": ASSET_PREFIX + asset, "source": str(source),
            "bytes": destination.stat().st_size, "sha256": digest(destination)}), encoding="utf-8")


def run_tool(command, label):
    """Run a build tool with output redirected to a file.

    Redirecting to a file instead of using capture_output also keeps the script
    usable under restricted environments where creating a pipe for a child
    process is denied (Windows sandboxes reject CreatePipe with EPERM)."""
    log = A9 / "artifacts" / (label + ".log")
    log.parent.mkdir(parents=True, exist_ok=True)
    with open(log, "w", encoding="utf-8", errors="replace") as stream:
        code = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT).returncode
    if code:
        raise SystemExit(label + " failed (exit " + str(code) + "):\n" +
                         log.read_text(encoding="utf-8", errors="replace"))


def compile_java(sources, destination):
    destination.mkdir(parents=True, exist_ok=True)
    command = ["javac", "--release", "8", "-cp", str(ANDROID_JAR), "-d", str(destination)] + [str(x) for x in sources]
    run_tool(command, "javac")


def d8(classes, destination):
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    class_files = [str(x) for x in classes.rglob("*.class")]
    command = ["cmd", "/c", str(D8), "--min-api", "28", "--lib", str(ANDROID_JAR),
               "--output", str(destination)] + class_files
    run_tool(command, "d8")


if __name__ == "__main__":
    main()
