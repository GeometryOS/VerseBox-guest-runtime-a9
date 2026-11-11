#!/usr/bin/env python3
"""Exercise the real paired codec on a GPU pbuffer; no desktop claim is made."""
import json
import os
from pathlib import Path
import re
import select
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "artifacts/container/graphics"
BIN = OUT / "bin"
CODEC = (OUT / "codec-id.txt").read_text().strip()
HELLO = ("A10VM-GL2-P1 checksum=1 " + CODEC).encode() + b"\0"
OPCODES = dict(re.findall(r"#define\s+OP_(\w+)\s+(\d+)",
                          (OUT / "generated/gl2_opcodes.h").read_text()))


def receive_exact(connection, size):
    data = b""
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise AssertionError("Unexpected handshake EOF")
        data += chunk
    return data


def host_case(directory, label, action, expected_error=None):
    path = str(Path(directory) / (label + ".sock"))
    environment = {"PATH": "/system/bin", "TMPDIR": directory}
    process = subprocess.Popen([str(BIN / "codec-host"), path], env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = b""
    drained = False
    try:
        deadline = time.monotonic() + 8
        while b"HOST_READY\n" not in output:
            remaining = deadline - time.monotonic()
            assert remaining > 0, "Host readiness timeout"
            assert select.select([process.stdout], [], [], remaining)[0], "Host readiness timeout"
            chunk = os.read(process.stdout.fileno(), 4096)
            assert chunk, output.decode(errors="replace")
            output += chunk
        client = action(path, environment)
        tail, _ = process.communicate(timeout=8)
        output += tail
        drained = True
        text = output.decode(errors="replace")
        if expected_error:
            assert process.returncode == 1 and expected_error in text, text
        else:
            assert process.returncode == 0 and "HOST_OK frames=32" in text, text
        return {"name": label, "passed": True, "hostExit": process.returncode,
                "host": text, "client": client}
    except Exception as error:
        if process.poll() is None:
            process.kill()
        if not drained:
            tail, _ = process.communicate(timeout=3)
            output += tail
        failure = {"name": label, "passed": False, "error": str(error),
                   "hostExit": process.returncode,
                   "host": output.decode(errors="replace")}
        (OUT / "native-report.json").write_text(json.dumps(failure, indent=2) + "\n")
        raise AssertionError(json.dumps(failure, indent=2)) from error
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)


def positive(path, environment):
    command = [str(BIN / "codec-client"), path]
    run = subprocess.run(command, env=environment,
                         capture_output=True, text=True, timeout=10)
    assert run.returncode == 0 and "CLIENT_OK frames=32 pixels=64" in run.stdout, {
        "exit": run.returncode, "stdout": run.stdout, "stderr": run.stderr}
    return {"exit": run.returncode, "stdout": run.stdout, "stderr": run.stderr}


def malformed(payload, wrong_handshake=False):
    def send(path, _environment):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(3)
            connection.connect(path)
            hello = bytes([HELLO[0] ^ 1]) + HELLO[1:] if wrong_handshake else HELLO
            connection.sendall(hello)
            if not wrong_handshake:
                assert receive_exact(connection, len(HELLO)) == HELLO
                connection.sendall(payload)
            connection.shutdown(socket.SHUT_WR)
        return {"negativeTestBytes": len(payload), "wrongHandshake": wrong_handshake}
    return send


def main():
    tests = []
    (OUT / "native-report.json").write_text(json.dumps({
        "gate": "P1a-native-paired-codec-pbuffer", "passed": False, "state": "running"}) + "\n")
    stream = subprocess.run([str(BIN / "stream-test")], capture_output=True, text=True,
                            env={"PATH": "/system/bin"}, timeout=5)
    assert stream.returncode == 0 and stream.stdout.startswith("PASS "), stream.stdout + stream.stderr
    tests.append({"name": "stream_and_checksum", "passed": True, "output": stream.stdout})
    with tempfile.TemporaryDirectory(prefix="a10vm-gl-", dir=os.environ.get("TMPDIR")) as directory:
        tests.append(host_case(directory, "paired_gpu", positive))
        tests.append(host_case(directory, "fingerprint", malformed(b"", True), "fingerprint mismatch"))
        color = int(OPCODES["glClearColor"])
        # Checksum v1 checks reversed length and packet order, not pixel content.
        corrupt = struct.pack("<II4fII", color, 32, 0., 0., 0., 1., 0x18000000, 1)
        tests.append(host_case(directory, "sequence", malformed(corrupt), "checksumCalculator failure"))
        viewport = int(OPCODES["glViewport"])
        tests.append(host_case(directory, "truncated", malformed(struct.pack("<II", viewport, 32) + b"xx"), "Truncated"))
        tests.append(host_case(directory, "length", malformed(struct.pack("<II", viewport, 0xFFFFFFFF)), "command length"))
        tests.append(host_case(directory, "unknown", malformed(struct.pack("<II", 0x7FFFFFFF, 8)), "outside P1 GLES subset"))
        pixel_store = int(OPCODES["glPixelStorei"])
        layout = struct.pack("<6I", pixel_store, 24, 0x0D04, 1, 0x08000000, 0)
        tests.append(host_case(directory, "pixel_layout", malformed(layout), "outside P1 GLES subset"))
    report = {"gate": "P1a-native-paired-codec-pbuffer", "passed": True, "codec": CODEC,
              "android10RuntimeVerified": False, "surfaceVerified": False,
              "desktopVerified": False, "tests": tests}
    (OUT / "native-report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    for test in tests:
        print("PASS", test["name"])
    print("Report:", OUT / "native-report.json")


if __name__ == "__main__":
    main()
