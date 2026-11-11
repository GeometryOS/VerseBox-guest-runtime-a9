#!/usr/bin/env python3
"""Generate typed GLES decoder adapters from the paired emugen output."""
from pathlib import Path
import re
import sys

generated, destination = map(Path, sys.argv[1:])
header = (generated / "gl2_server_proc.h").read_text()
adapters = []
for result, name, arguments in re.findall(
        r"typedef (.*?) \(gl2_APIENTRY \*(\w+)_dec_server_proc_t\) \((.*?)\);", header):
    types = arguments.split(",") if arguments else []
    declarations = ", ".join(f"{kind.strip()} p{i}" for i, kind in enumerate(types))
    values = ", ".join(f"p{i}" for i in range(len(types)))
    adapters.append(f"gl_.{name}_dec = [](void* self{', ' if declarations else ''}{declarations})"
                    f" -> {result} {{\n"
                    f"    auto* gl = static_cast<gl2_decoder_context_t*>(self);\n"
                    f"    if (!gl->{name}) throw std::runtime_error(\"Missing host {name}\");\n"
                    f"    return gl->{name}({values});\n}};")
destination.write_text("// Generated from the paired GLES protocol.\n" + "\n".join(adapters) + "\n")

