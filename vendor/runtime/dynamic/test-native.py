#!/usr/bin/env python3
"""Exercise the source-built loader/libc and reject host-runtime fallback."""
import json
import pathlib
import re
import shutil
import subprocess
import tempfile
from collections import Counter

root = pathlib.Path(__file__).resolve().parents[3]
runtime = root / "artifacts/container/runtime"
output = runtime / "dynamic"
output.mkdir(exist_ok=True)
linker = runtime / "bin/source-linker64"
program = runtime / "bin/dynamic-probe"
module = runtime / "lib/liba10vm-tls-test.so"
libraries = {name: runtime / "lib" / name for name in ("libc.so", "libdl.so")}
allowed = {str(path.resolve()) for path in (linker, program, *libraries.values())}
required = {
    "dynamic_property_init_order", "dynamic_shared_globals", "dynamic_loader_libc_distinct",
    "dynamic_exec_child_properties", "dynamic_exec_child_runtime", "dynamic_exec_chain",
    "bootstrap_key_create", "bootstrap_thread_create", "bootstrap_thread_join",
    "bootstrap_tls_errno_isolation", "bootstrap_thread_destructors", "bootstrap_key_delete",
    "dynamic_allocator_barrier", "dynamic_allocator_concurrent", "dynamic_dlopen",
    "dynamic_dlsym", "dynamic_tls_main_initialization", "dynamic_tls_existing_thread",
    "dynamic_tls_generation", "dynamic_tls_dso_pinned", "dynamic_tls_dso_unloaded",
    "bootstrap_identity_files", "dynamic_stdio_buffered",
    "dynamic_namespace_created", "dynamic_namespace_shared_linker", "dynamic_namespace_unloaded",
}

for path in (linker, program, module, *libraries.values()):
    elf = subprocess.check_output(["llvm-readelf", "-hlWd", str(path)], text=True)
    assert "AArch64" in elf and "DYN" in elf, path
    assert "RUNPATH" not in elf and "RPATH" not in elf, path
    needed = set(re.findall(r"Shared library: \[(.*?)\]", elf))
    assert needed <= {"libc.so", "libdl.so", "ld-android.so"}, (path, needed)
    if path == linker:
        assert not needed and "INTERP" not in elf
        assert not subprocess.check_output(["llvm-nm", "-u", str(path)], text=True).strip()
    if path == program:
        tls = next(line.split() for line in elf.splitlines() if line.strip().startswith("TLS "))
        assert int(tls[-1], 16) >= 64, tls

help_result = subprocess.run([str(linker), "--help"], capture_output=True, text=True, timeout=10)
assert help_result.returncode == 0 and "Usage" in help_result.stdout, help_result
cases = []
with tempfile.TemporaryDirectory(prefix="dynamic-", dir=output) as temporary:
    directory = pathlib.Path(temporary)
    seed = directory / "instance"
    environment = {"PATH": "/system/bin", "TMPDIR": str(directory)}
    seeded = subprocess.run([
        str(root / "artifacts/container/properties/bin/property-probe"), "run", str(seed),
        "dynamic-seed", "/system/bin/linker64",
    ], env=environment, capture_output=True, text=True, timeout=10)
    assert seeded.returncode == 0, seeded.stdout + seeded.stderr
    incomplete = seed / "incomplete"
    incomplete.mkdir()
    shutil.copyfile(seed / "property_info", incomplete / "property_info")

    def configuration(name, missing_libc=False):
        directory_path = directory / name
        directory_path.mkdir()
        for library_name, library_path in libraries.items():
            if not (missing_libc and library_name == "libc.so"):
                (directory_path / library_name).symlink_to(library_path)
        config = directory_path / "ld.config.txt"
        config.write_text(
            f"dir.probe = {program.parent}\n[probe]\n"
            "namespace.default.isolated = true\n"
            f"namespace.default.search.paths = {directory_path}\n"
            f"namespace.default.permitted.paths = {runtime}\n", encoding="utf-8")
        return config

    config = configuration("complete")
    for mode in ("reader", "missing", "relative", "incomplete"):
        sdk = "29" if mode == "reader" else ""
        env = dict(environment, A10VM_LD_CONFIG=str(config))
        if mode != "missing":
            env["A10VM_PROPERTIES"] = {"reader": str(seed), "relative": "relative",
                                      "incomplete": str(incomplete)}[mode]
        result = subprocess.run([str(linker), str(program), mode, "dynamic-local", str(module)],
                                env=env, capture_output=True, text=True, timeout=10)
        text = result.stdout + result.stderr
        (output / f"local-{mode}.log").write_text(text, encoding="utf-8")
        assert result.returncode == 0, (mode, result.returncode, text)
        checks = {line.split("\t")[1] for line in text.splitlines()
                  if line.startswith("CHECK\t") and line.endswith("\t1")}
        assert required <= checks and "\t0\n" not in text, (mode, required - checks, text)
        for marker in (
            "dynamic_exec_child_started=1\n",
            f"dynamic_exec_child_mode={mode}\n",
            f"dynamic_exec_child_sdk={sdk}\n",
            "dynamic_exec_child_complete=1\n",
            "thread_rounds=2\nkey_destructors=2\ncpp_destructors=2\n",
            "allocator_workers=4\nallocator_rounds_per_worker=64\n",
            "dynamic_tls_rounds=2\ndynamic_tls_destructors=2\ndynamic_module_unloads=2\n",
    "dynamic_complete=1\ndynamic_fini_complete=1\ndynamic_stdio_flush_complete=1\n",
        ):
            assert marker in text, (mode, marker, text)
        assert "preinit_sdk=\n" in text, (mode, text)
        for stage in ("constructor", "main"):
            assert f"{stage}_sdk={sdk}\n" in text, (mode, stage, text)
        assert f"source_linker={linker}\n" in text, (mode, text)
        assert f"source_libc={libraries['libc.so']}\n" in text, (mode, text)
        maps = [line[4:].split(None, 5) for line in text.splitlines() if line.startswith("MAP\t")]
        mapped_elfs = {str(pathlib.Path(entry[5]).resolve()) for entry in maps
                       if len(entry) == 6 and "x" in entry[1] and entry[5].startswith("/")}
        assert mapped_elfs == allowed, (mode, mapped_elfs)
        other_maps = [entry for entry in maps if len(entry) == 6 and entry[5].startswith("/")
                      and str(pathlib.Path(entry[5]).resolve()) not in allowed]
        if mode == "reader":
            assert len(other_maps) == 12 and all(entry[1] == "r--s" and int(entry[2], 16) == 0 and
                   pathlib.Path(entry[5]).resolve().parent == seed for entry in other_maps), other_maps
            assert Counter(pathlib.Path(entry[5]).name for entry in other_maps) == {
                "property_info": 4, "properties_serial": 4, "a10vm_private_prop": 4}, other_maps
        else:
            assert not other_maps, other_maps
        assert "/dev/__properties__" not in text
        cases.append({"mode": mode, "passed": True, "checks": len(checks), "sdk": sdk,
                      "executableMappings": sorted(mapped_elfs), "readonlyPropertyMaps": len(other_maps)})

    bad_config = directory / "bad.config"
    bad_config.write_text("[wrong-section]\nnamespace.default.isolated = true\n", encoding="utf-8")
    failures = {
        "missing-config": (None, "requires an existing absolute private configuration"),
        "relative-config": ("relative", "requires an existing absolute private configuration"),
        "bad-config": (str(bad_config), "private linker configuration was rejected"),
        "missing-libc": (str(configuration("missing-libc", True)), "libc.so"),
    }
    rejected = []
    for mode, (config_path, diagnostic) in failures.items():
        env = dict(environment, A10VM_PROPERTIES=str(seed))
        if config_path is not None:
            env["A10VM_LD_CONFIG"] = config_path
        result = subprocess.run([str(linker), str(program), "reader", "dynamic-local", str(module)],
                                env=env, capture_output=True, text=True, timeout=10)
        text = result.stdout + result.stderr
        (output / f"local-{mode}.log").write_text(text, encoding="utf-8")
        assert result.returncode != 0 and diagnostic in text and "dynamic_mode=" not in text, (mode, text)
        if mode == "missing-libc":
            assert "not found" in text
        rejected.append({"mode": mode, "passed": True, "exitCode": result.returncode})

(output / "local-report.json").write_text(json.dumps({
    "passed": True, "loaderFromSource": True, "dynamicRuntimeIntegrated": True,
    "concurrentAllocatorVerified": True, "cases": cases, "rejected": rejected,
}, indent=2) + "\n", encoding="utf-8")
print("Source linker/libc: four property cases and four load failures passed; allocator/TLS/unload verified.")
