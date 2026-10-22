#!/usr/bin/env python3
"""Run the private loader through directory aliases without root privileges."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

project = Path(__file__).resolve().parents[3]
runtime = project / "artifacts/container/runtime"
linker = runtime / "bin/source-linker64"
program = runtime / "bin/dynamic-probe"
assert os.getuid() >= 10000, "Run with the ordinary Termux UID."

with tempfile.TemporaryDirectory(prefix="config-", dir=runtime / "dynamic") as temporary:
    instance = Path(temporary)
    executable_directory = instance / "native"
    executable_directory.mkdir()
    executable = executable_directory / "probe"
    shutil.copyfile(program, executable)
    executable.chmod(0o755)
    alias = instance / "installation-alias"
    alias.symlink_to(executable_directory, target_is_directory=True)
    config = instance / "ld.config.txt"

    def launch(directory, section="guest", command=executable, guest=False):
        config.write_text(
            f"dir.{section} = {directory}\n[{section}]\n"
            "namespace.default.isolated = true\n"
            f"namespace.default.search.paths = {runtime / 'lib'}\n"
            f"namespace.default.permitted.paths = {runtime}:{instance}\n",
            encoding="utf-8")
        environment = {"PATH": "/system/bin", "A10VM_LD_CONFIG": str(config)}
        if guest:
            environment.update(A10VM_ROOTFS=str(instance / "rootfs"),
                               A10VM_HOST_ROOT=str(runtime), A10VM_STATE_ROOT=str(instance))
        return subprocess.run(
            [str(linker), str(command), "exec-child", "missing", "config-regression"],
            env=environment,
            capture_output=True, text=True, timeout=5)

    def require_success(name, result):
        output = result.stdout + result.stderr
        assert result.returncode == 0 and "dynamic_exec_child_complete=1\n" in output, (
            name, result.returncode, output)
        assert f"dynamic_exec_child_libc={runtime / 'lib/libc.so'}\n" in output, output
        print(f"PASS {name}", flush=True)

    require_success("readable-directory-alias", launch(alias))
    guest_program = instance / "rootfs/data/local/tmp/terminal-helper"
    guest_program.parent.mkdir(parents=True)
    shutil.copyfile(program, guest_program)
    guest_program.chmod(0o755)
    require_success("guest-terminal-helper", launch(instance, command=guest_program, guest=True))
    require_success("guest-terminal-logical-path", launch(
        instance, command=Path("/data/local/tmp/terminal-helper"), guest=True))
    executable_directory.chmod(0o111)
    try:
        assert not os.access(alias, os.R_OK) and os.access(alias, os.X_OK)
        require_success("execute-only-directory", launch(executable_directory))
        require_success("execute-only-directory-alias", launch(alias))
        require_success("registry-directory-alias", launch(alias, "registry", alias / "probe"))
    finally:
        executable_directory.chmod(0o755)

    missing = instance / "missing-section.config"
    missing.write_text(f"dir.guest = {alias}\n[wrong]\n", encoding="utf-8")
    unmatched = instance / "unmatched.config"
    unrelated = instance / "unrelated"
    unrelated.mkdir()
    unmatched.write_text(f"dir.guest = {unrelated}\n[guest]\n", encoding="utf-8")
    empty = instance / "empty.config"
    empty.write_text("", encoding="utf-8")
    for rejected in (missing, unmatched, empty):
        result = subprocess.run(
            [str(linker), str(executable), "exec-child", "missing", "config-regression"],
            env={"PATH": "/system/bin", "A10VM_LD_CONFIG": str(rejected)},
            capture_output=True, text=True, timeout=5)
        output = result.stdout + result.stderr
        assert result.returncode != 0 and "private linker configuration was rejected" in output, output
        assert str(rejected) in output and str(executable) in output, output
        assert "reason=" in output and "dynamic_exec_child_started" not in output, output
        print(f"PASS rejected-{rejected.stem}", flush=True)

print("Private linker configuration: six launch cases and three invalid configurations passed.")
