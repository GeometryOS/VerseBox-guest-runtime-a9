#!/usr/bin/env python3
"""Select Android 10's ARM64 shared-libc sources from its fixed Android.bp."""
import json
import pathlib
import re
import sys


class Blueprint:
    def __init__(self, path):
        token_pattern = r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/|\+=|\w+|[{}\[\]:=,+]'
        source = path.read_text(encoding="utf-8")
        self.tokens = []
        end = 0
        for match in re.finditer(token_pattern, source, re.S):
            if source[end:match.start()].strip():
                raise ValueError(f"Unparsed Blueprint text at {path}:{end}")
            token = match.group()
            if not token.startswith(("//", "/*")):
                self.tokens.append(token)
            end = match.end()
        if source[end:].strip():
            raise ValueError(f"Unparsed Blueprint tail: {path}")
        self.position = 0
        self.variables = {}
        self.modules = {}
        while self.position < len(self.tokens):
            kind = self.take()
            if self.tokens[self.position] == "{":
                module = self.value()
                if kind in ("cc_library", "cc_library_static", "cc_library_shared", "cc_defaults", "filegroup"):
                    name = module["name"]
                    if name in self.modules:
                        raise ValueError(f"Duplicate module: {name}")
                    self.modules[name] = module
            else:
                operation = self.take()
                value = self.value()
                if operation == "=":
                    self.variables[kind] = value
                elif operation == "+=":
                    self.variables[kind] += value
                else:
                    raise ValueError(f"Unexpected assignment: {operation}")

    def take(self, expected=None):
        token = self.tokens[self.position]
        self.position += 1
        if expected is not None and token != expected:
            raise ValueError(f"Expected {expected}, got {token}")
        return token

    def value(self):
        token = self.take()
        if token == "{":
            result = {}
            while self.tokens[self.position] != "}":
                key = self.take()
                self.take(":")
                result[key] = self.value()
                if self.tokens[self.position] != "}":
                    self.take(",")
            self.take("}")
        elif token == "[":
            result = []
            while self.tokens[self.position] != "]":
                result.append(self.value())
                if self.tokens[self.position] != "]":
                    self.take(",")
            self.take("]")
        elif token.startswith('"'):
            result = json.loads(token)
        elif token in ("true", "false"):
            result = token == "true"
        elif token.isdecimal():
            result = int(token)
        else:
            result = self.variables[token]
        if self.position < len(self.tokens) and self.tokens[self.position] == "+":
            self.take("+")
            result = result + self.value()
        return result


def select(upstream):
    base = upstream / "bionic/libc"
    blueprint = Blueprint(base / "Android.bp")
    selected = set()
    visited = set()

    def visit(name):
        if name in visited:
            return
        visited.add(name)
        if name == "libsystemproperties":
            selected.update("bionic/libc/system_properties/" + path for path in (
                "context_node.cpp", "contexts_split.cpp", "contexts_serialized.cpp",
                "prop_area.cpp", "prop_info.cpp", "system_properties.cpp"))
            return
        module = blueprint.modules[name]
        layers = [module, module.get("shared", {}),
                  module.get("arch", {}).get("arm64", {}),
                  module.get("multilib", {}).get("lib64", {})]
        sources = []
        excluded = set()
        for layer in layers:
            sources += layer.get("srcs", [])
            excluded.update(layer.get("exclude_srcs", []))
            for dependency in layer.get("whole_static_libs", []):
                if dependency == "libjemalloc5":
                    jemalloc = Blueprint(upstream / "external/jemalloc_new/Android.bp")
                    selected.update("external/jemalloc_new/" + path
                                    for path in jemalloc.variables["lib_src_files"])
                else:
                    visit(dependency)
        for pattern in sources:
            if pattern in excluded:
                continue
            if pattern.startswith(":"):
                visit(pattern[1:])
                continue
            matches = sorted(base.glob(pattern))
            if not matches:
                raise ValueError(f"Missing {name} source: {pattern}")
            selected.update(path.relative_to(upstream).as_posix() for path in matches)

    visit("libc")
    for path in sorted(selected):
        if not (upstream / path).is_file():
            raise ValueError(f"Missing selected input: {path}")
        print(path)


if __name__ == "__main__":
    select(pathlib.Path(sys.argv[1]).resolve())
