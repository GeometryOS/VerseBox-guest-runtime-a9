#!/usr/bin/env python3
"""Run the real guest pixel converters through the private Android 10 runtime."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "artifacts/container/runtime"
GRAPHICS = ROOT / "artifacts/container/graphics/desktop"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--benchmark", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="pixels-", dir=GRAPHICS) as temporary:
        config = Path(temporary) / "ld.config.txt"
        paths = [GRAPHICS / "lib", RUNTIME / "lib", ROOT / "artifacts/container/framework/lib"]
        config.write_text(f"dir.pixels = {GRAPHICS / 'bin'}\n[pixels]\n"
                          "namespace.default.isolated = true\nnamespace.default.search.paths = " +
                          ":".join(map(str, paths)) + "\nnamespace.default.permitted.paths = " +
                          str(ROOT / "artifacts/container") + "\n", encoding="utf-8")
        command = [str(RUNTIME / "bin/source-linker64"), str(GRAPHICS / "bin/format-conversions-check")]
        if args.benchmark:
            command.append("--benchmark")
        result = subprocess.run(command, env={"PATH": "/system/bin", "A10VM_LD_CONFIG": str(config)},
                                capture_output=True, text=True, timeout=15)
        print(result.stdout + result.stderr, end="")
        if not args.benchmark and "desktop_format_conversions=1\n" not in result.stdout:
            return 1
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
