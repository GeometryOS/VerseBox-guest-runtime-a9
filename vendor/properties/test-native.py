#!/usr/bin/env python3
"""Exercise real AOSP property mappings in this host UID before APK integration."""
import pathlib
import os
import shutil
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
output = root / "artifacts/container/properties"
binary = output / "bin/property-probe"
api_binary = output / "bin/property-api-probe"
expected = {
    "private_sdk29", "readonly_rewrite_rejected", "long_readonly_callback",
    "long_mutable_rejected", "cross_process_property_wait", "cross_process_global_wait",
    "reader_identity", "reader_exit", "reader_readonly_mappings", "reader_write_rejected",
    "property_timeout", "global_timeout", "instances_isolated", "reopen_unmapped",
    "failed_open_unmapped", "info_writable_rejected", "area_symlink_rejected",
    "writer_shared_mappings", "second_instance_unmapped",
    "writer_global_update_complete", "global_wait_ready", "property_wait_blocked", "global_wait_blocked",
}


def run_case(directory, log):
    result = subprocess.run(
        [str(binary), "run", str(directory), "local-properties", "/system/bin/linker64"],
        capture_output=True, text=True, timeout=10,
    )
    (output / log).write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, (result.returncode, result.stdout[-2500:], result.stderr)
    assert "probe_complete=1" in result.stdout
    checks = {line.split("\t")[1] for line in result.stdout.splitlines()
              if line.startswith("CHECK\t") and line.endswith("\t1")}
    assert expected <= checks, expected - checks
    return result.stdout, len(checks)


def api_cases(directory, label):
    incomplete = directory / "incomplete"
    incomplete.mkdir()
    shutil.copyfile(directory / "property_info", incomplete / "property_info")
    for mode in ("reader", "missing", "relative", "incomplete", "writer"):
        environment = dict(os.environ)
        environment.pop("A10VM_PROPERTIES", None)
        if mode == "reader":
            environment["A10VM_PROPERTIES"] = str(directory)
        elif mode == "relative":
            environment["A10VM_PROPERTIES"] = "relative"
        elif mode == "incomplete":
            environment["A10VM_PROPERTIES"] = str(incomplete)
        path = directory / "api-writer" if mode == "writer" else directory
        result = subprocess.run([str(api_binary), mode, str(path), "local-api"],
                                env=environment, capture_output=True, text=True, timeout=10)
        (output / f"local-api-{label}-{mode}.log").write_text(
            result.stdout + result.stderr, encoding="utf-8")
        assert result.returncode == 0, (mode, result.returncode, result.stdout[-2500:], result.stderr)
        assert "api_complete=1" in result.stdout and "api_sdk=29" in result.stdout
        assert "CHECK\tapi_early_no_heap_calls\t1" in result.stdout
        if mode != "writer":
            assert "CHECK\tapi_instance_switch_rejected\t1" in result.stdout
    return 5


with tempfile.TemporaryDirectory(prefix="properties-", dir=output) as temporary:
    directory = pathlib.Path(temporary)
    _, direct_checks = run_case(directory / "instance", "local-test.log")
    direct_api = api_cases(directory / "instance", "direct")
    real = directory / "real"
    real.mkdir()
    alias = directory / "alias"
    alias.symlink_to(real, target_is_directory=True)
    text, alias_checks = run_case(alias / "instance", "local-alias-test.log")
    assert f"reader_directory={alias / 'instance'}\n" in text
    maps = [line for line in text.splitlines() if line.startswith("PROPERTY_MAP\t")]
    assert maps and all(str(alias) not in line for line in maps), maps
    assert any(str(real) in line for line in maps), maps
    alias_api = api_cases(alias / "instance", "alias")
print(f"Properties native checks: direct={direct_checks}, alias={alias_checks}; "
      f"API cases: direct={direct_api}, alias={alias_api}; temporary instances removed.")
