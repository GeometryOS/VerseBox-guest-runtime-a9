#!/usr/bin/env python3
"""Exercise guest filesystem and exec paths using the caller's ordinary UID."""
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import uuid
from verify import identities

project = pathlib.Path(__file__).resolve().parents[3]
runtime = project / "artifacts/container/runtime"
output = runtime / "filesystem"
output.mkdir(exist_ok=True)
linker = runtime / "bin/source-linker64"
program = runtime / "bin/filesystem-probe"
required = {
    "fs_config_absolute_open", "fs_config_syscall_open", "fs_config_directory_enumeration",
    "fs_config_relative_dirfd", "fs_missing_kernel_config_no_host_directory",
    "fs_syscall_open", "fs_syscall_mkdir", "fs_syscall_rename", "fs_syscall_stat", "fs_syscall_unlink",
    "fs_create_absolute_symlink", "fs_create_symlinkat_dirfd", "fs_syscall_symlinkat",
    "fs_create_symlink_missing_parent",
    "fs_ordinary_uid", "fs_absolute_open", "fs_absolute_symlink", "fs_relative_symlink",
    "fs_apex_symlink", "fs_parent_component", "fs_access", "fs_stat", "fs_statvfs_guest", "fs_lstat_symlink",
    "fs_readlink", "fs_readlink_fd_roundtrip", "fs_readlink_fd_truncated", "fs_syscall_readlink_fd",
    "fs_readlink_fd_relative", "fs_readlink_zero_size", "fs_readlink_null_buffer",
    "fs_readlink_host_target", "fs_readlink_guest_cwd",
    "fs_missing_guest_no_host_file", "fs_symlink_loop", "fs_openat_dirfd",
    "fs_openat_absolute_symlink", "fs_chdir", "fs_physical_cwd", "fs_getcwd", "fs_relative_from_cwd",
    "fs_private_write", "fs_rename", "fs_unlink", "fs_mkdir_rmdir", "fs_child_argv0",
    "fs_mkdir_trailing_slash", "fs_mkdirat_trailing_slashes", "fs_syscall_mkdir_trailing_slash",
    "fs_mkdir_trailing_existing", "fs_mkdir_trailing_missing_parent", "fs_mkdir_trailing_regular_file",
    "fs_mkdir_trailing_symlink", "fs_trailing_directory_persistent_write",
    "fs_download_truncate", "fs_download_truncate64", "fs_download_syscall_truncate", "fs_download_truncate_missing",
    "fs_child_paths", "fs_child_properties", "fs_child_cwd", "fs_guest_exec_chain",
    "fs_missing_exec", "bootstrap_identity_files",
    "fs_realpath_host_executable", "fs_realpath_guest_symlink", "fs_realpath_missing",
    "fs_guest_chmod", "fs_guest_ownership", "fs_guest_fstat_owner",
    "fs_kernel_owner_unchanged", "fs_guest_owner_after_rename",
    "fs_guest_xattr_write", "fs_guest_xattr_read", "fs_guest_xattr_descriptor", "fs_guest_xattr_remove",
    "fs_guest_inotify", "fs_guest_socket_roundtrip",
    "fs_guest_utimes", "fs_guest_futimens", "fs_guest_utimensat_dirfd",
    "fs_guest_utimensat_symlink", "fs_guest_utimensat_missing",
    "fs_script_arguments", "fs_script_guest_paths", "fs_guest_script_exec", "fs_guest_script_loop",
}
assert os.getuid() >= 10000, "Run this harness using the ordinary Termux UID."
with tempfile.TemporaryDirectory(prefix="fs-", dir=output) as temporary:
    instance = pathlib.Path(temporary)
    rootfs = instance / "rootfs"
    for path in ("system/etc", "system/fonts", "system/bin", "system/runtime", "data/local/tmp", "apex", "config"):
        (rootfs / path).mkdir(parents=True)
    (instance / "s").mkdir()
    (rootfs / "dev").mkdir()
    (rootfs / "dev/socket").symlink_to(instance / "s")
    nonce = uuid.uuid4().hex
    (rootfs / "system/etc/a10vm-marker").write_text(nonce, encoding="utf-8")
    (rootfs / "config/a10vm-marker").write_text(nonce, encoding="utf-8")
    (rootfs / "system/etc/fonts.xml").write_text(nonce, encoding="utf-8")
    (rootfs / "system/fonts/Roboto-Regular.ttf").write_text(nonce, encoding="utf-8")
    (rootfs / "system/runtime/a10vm-marker").write_text(nonce, encoding="utf-8")
    shutil.copyfile(runtime / "lib/libdl.so", rootfs / "data/local/tmp/owner-library.so")
    for name, script in {
        "exec-script": "#! /system/bin/filesystem-child interpreter  \n",
        "exec-loop": "#!/data/local/tmp/exec-loop\n",
    }.items():
        path = rootfs / "data/local/tmp" / name
        path.write_text(script, encoding="utf-8")
        path.chmod(0o755)
    for path, target in {
        "system/etc/absolute-marker": "/system/etc/a10vm-marker",
        "system/etc/relative-marker": "a10vm-marker",
        "system/etc/loop-a": "loop-b", "system/etc/loop-b": "loop-a",
        "apex/runtime": "/system/runtime",
        "system/bin/filesystem-child": str(program),
    }.items():
        (rootfs / path).symlink_to(target)
    seed = instance / "properties"
    env = {"PATH": "/system/bin", "TMPDIR": str(instance)}
    subprocess.run([str(project / "artifacts/container/properties/bin/property-probe"),
                    "run", str(seed), nonce, "/system/bin/linker64"],
                   env=env, capture_output=True, text=True, timeout=10, check=True)
    libraries = instance / "lib"
    libraries.mkdir()
    for name in ("libc.so", "libdl.so"):
        (libraries / name).symlink_to(runtime / "lib" / name)
    config = libraries / "ld.config.txt"
    config.write_text(
        f"dir.probe = {program.parent}\n[probe]\nnamespace.default.isolated = true\n"
        f"namespace.default.search.paths = {libraries}\n"
        f"namespace.default.permitted.paths = {runtime}:/data/local/tmp\n", encoding="utf-8")
    env.update(A10VM_ROOTFS=str(rootfs), A10VM_HOST_ROOT=str(runtime), A10VM_STATE_ROOT=str(instance),
               A10VM_TEST_EXECUTABLE=str(program),
               A10VM_EXEC_LINKER=str(linker), A10VM_PROPERTIES=str(seed), A10VM_LD_CONFIG=str(config))
    subprocess.run([str(linker), str(program), "owner-profile-setup", nonce], env=env,
                   capture_output=True, text=True, timeout=3, check=True)
    owner = subprocess.run([str(linker), str(program), "owner-filter", nonce], env=env,
                           capture_output=True, text=True, timeout=3)
    (output / "owner-filter.log").write_text(owner.stdout + owner.stderr, encoding="utf-8")
    assert owner.returncode == 0 and all("CHECK\t" + name + "\t1\n" in owner.stdout for name in (
        "fs_x5_library_preloaded", "fs_owner_cache_initial", "fs_owner_cache_invalidated",
        "fs_x5_library_fstat", "fs_x5_owned_fstat", "fs_x5_linker_preloaded",
        "fs_x5_linker_reopened")), ("X5 descriptor stat rejected", owner)
    sandbox = subprocess.run([str(linker), str(program), "owner-sandbox", nonce], env=env,
                             capture_output=True, text=True, timeout=3)
    (output / "owner-sandbox.log").write_text(sandbox.stdout + sandbox.stderr, encoding="utf-8")
    assert sandbox.returncode == 0 and "CHECK\tfs_x5_first_library_fstat\t1\n" in sandbox.stdout, \
        ("X5 first library stat rejected", sandbox)
    for mode in ("owner-cold-tsync", "owner-cold-prctl"):
        cold = subprocess.run([str(linker), str(program), mode, nonce], env=env,
                              capture_output=True, text=True, timeout=3)
        (output / (mode + ".log")).write_text(cold.stdout + cold.stderr, encoding="utf-8")
        assert cold.returncode == 0 and "\t0\n" not in cold.stdout and all(
            "CHECK\t" + name + "\t1\n" in cold.stdout for name in (
                "fs_owner_filter_installed", "fs_owner_cold_profile_stat", "fs_owner_changed_ctime_fstat",
                "fs_owner_profile_thread_stat", "fs_owner_existing_thread_filter",
                "fs_owner_original_filter_errno", "fs_owner_direct_xattr_blocked"
            )), ("X5 cold metadata rejected", mode, cold)
    result = subprocess.run([str(linker), str(program), "parent", nonce], env=env,
                            capture_output=True, text=True, timeout=10)
    font = subprocess.run([str(linker), str(program), "font-filter", nonce], env=env,
                          capture_output=True, text=True, timeout=3)
    (output / "font-filter.log").write_text(font.stdout + font.stderr, encoding="utf-8")
    assert font.returncode == 0 and all("CHECK\t" + name + "\t1\n" in font.stdout for name in (
        "fs_font_readlink_blocked", "fs_font_config_seccomp", "fs_font_file_seccomp")), font
    virtual = subprocess.run([str(linker), str(program), "virtual-identity", nonce],
        env=dict(env, A10VM_GUEST_UID="1000", A10VM_GUEST_GID="1000"),
        capture_output=True, text=True, timeout=3)
    (output / "virtual-identity.log").write_text(virtual.stdout + virtual.stderr, encoding="utf-8")
    assert virtual.returncode == 0 and "CHECK\tfs_identity_kernel_uid\t1\n" in virtual.stdout, virtual
    text = result.stdout + result.stderr
    (output / "local.log").write_text(text, encoding="utf-8")
    checks = {line.split("\t")[1] for line in text.splitlines()
              if line.startswith("CHECK\t") and line.endswith("\t1")}
    allowed = {linker, program, runtime / "lib/libc.so", runtime / "lib/libdl.so"}
    identity = identities(text, os.getuid(), nonce, allowed, seed)
    negative = {
        "child-euid": text.replace(f"euid={os.getuid()}\n", "euid=0\n", 1),
        "child-capabilities": text.replace("cap_eff=0000000000000000\n",
                                            "cap_eff=0000000000000001\n", 1),
        "child-missing-context": text.replace("selinux=", "missing_context=", 1),
        "host-libc": text.replace(str(runtime / "lib/libc.so"), "/system/lib64/libc.so"),
    }
    rejected = {name: candidate != text and not identities(
        candidate, os.getuid(), nonce, allowed, seed)["passed"]
        for name, candidate in negative.items()}
    completed = "filesystem_child_complete=1\n" in text and "filesystem_complete=1\n" in text
    passed = (result.returncode == 0 and required <= checks and "\t0\n" not in text
              and completed and identity["passed"] and all(rejected.values()))
    report = {"passed": passed, "ordinaryUid": os.getuid(), "checks": len(checks),
              "missingChecks": sorted(required - checks), "exitCode": result.returncode,
              "completeG1": False, "desktopVerified": False, "identity": identity,
              "validatorNegativeCases": rejected}
    (output / "local-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    assert passed, report
print(f"Rootless guest filesystem/exec: {len(checks)} checks; per-process UID/caps/maps and four validator negatives passed.")
