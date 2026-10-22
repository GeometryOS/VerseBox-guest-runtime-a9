#!/usr/bin/env python3
import hashlib
import json
import pathlib
import shlex
import shutil
import subprocess
import sys

object_path = pathlib.Path(sys.argv[1])
command = sys.argv[2:]
dependency_path = pathlib.Path(str(object_path) + ".d")
cache_path = pathlib.Path(str(object_path) + ".cache.json")
compiler = pathlib.Path(shutil.which(command[0])).resolve()
compiler_stat = compiler.stat()
identity = [str(compiler), compiler_stat.st_size, compiler_stat.st_mtime_ns]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def signature():
    dependency = dependency_path.read_text(encoding="utf-8")
    names = shlex.split(dependency.replace("\\\n", " ").split(":", 1)[1])
    inputs = {name: digest(pathlib.Path(name)) for name in sorted(set(names))}
    return {"command": command, "compiler": identity, "inputs": inputs,
            "object": digest(object_path)}


try:
    cached = json.loads(cache_path.read_text(encoding="utf-8"))
    if cached == signature():
        sys.exit(0)
except (OSError, ValueError, IndexError):
    pass

cache_path.unlink(missing_ok=True)
result = subprocess.run(command)
if result.returncode:
    sys.exit(result.returncode)
cache_path.write_text(json.dumps(signature(), sort_keys=True) + "\n", encoding="utf-8")
