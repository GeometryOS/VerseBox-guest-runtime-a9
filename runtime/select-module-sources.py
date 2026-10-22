#!/usr/bin/env python3
"""Print the ARM64 source files of one AOSP 9 blueprint module.

Usage: select-module-sources.py <upstream> <blueprint> <module> <arch>
"""
import importlib.util
import pathlib
import sys

_here = pathlib.Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("a9_blueprint", _here / "select-libc-sources.py")
_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_module)
Blueprint = _module.Blueprint


def main():
    upstream, blueprint_path, name, arch = sys.argv[1:5]
    upstream = pathlib.Path(upstream).resolve()
    blueprint_path = pathlib.Path(blueprint_path).resolve()
    blueprint = Blueprint(blueprint_path)
    module = blueprint.modules[name]
    layers = [module, module.get("shared", {}),
              module.get("arch", {}).get(arch, {}),
              module.get("multilib", {}).get("lib64", {})]
    selected = []
    excluded = set()
    for layer in layers:
        selected += layer.get("srcs", [])
        excluded.update(layer.get("exclude_srcs", []))
    base = pathlib.Path(blueprint_path).parent
    for pattern in selected:
        if pattern in excluded:
            continue
        matches = sorted(base.glob(pattern))
        if not matches:
            raise SystemExit("missing source: " + pattern)
        for path in matches:
            print(path.relative_to(upstream).as_posix())


if __name__ == "__main__":
    main()
