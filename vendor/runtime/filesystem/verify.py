"""Validate each native process independently from its captured identity/maps."""
from collections import Counter
import pathlib
import re


def identities(text, uid, nonce, allowed, seed):
    blocks = re.findall(
        r"filesystem_identity_begin=(parent|child)\n(.*?)filesystem_identity_end=\1\n",
        text, re.S,
    )
    processes = []
    allowed = {str(pathlib.Path(path).resolve()) for path in allowed}
    seed = pathlib.Path(seed).resolve()
    for role, block in blocks:
        fields = {}
        for line in block.splitlines():
            key, separator, value = line.partition("=")
            if separator:
                fields.setdefault(key, []).append(value)
        unique = all(len(fields.get(key, [])) == 1
                     for key in ("nonce", "pid", "uid", "euid", "cap_eff", "selinux"))
        values = {key: value[0] for key, value in fields.items()}
        maps = [line[4:].split(None, 5) for line in block.splitlines()
                if line.startswith("MAP\t")]
        executable = set()
        properties = Counter()
        private = True
        for entry in maps:
            if len(entry) != 6 or not entry[5].startswith("/"):
                continue
            path = pathlib.Path(entry[5]).resolve()
            if "x" in entry[1]:
                executable.add(str(path))
            if str(path) in allowed:
                continue
            private &= path.parent == seed and entry[1] == "r--s" and int(entry[2], 16) == 0
            properties[path.name] += 1
        property_maps = private and properties == {
            "property_info": 2, "properties_serial": 2, "a10vm_private_prop": 2,
        }
        ordinary = (unique and uid >= 10000 and values.get("uid") == str(uid)
                    and values.get("euid") == str(uid)
                    and values.get("nonce") == nonce
                    and values.get("cap_eff") == "0000000000000000"
                    and re.fullmatch(r"u:r:untrusted_app(?:_[0-9]+)?:s0(?::.*)?",
                                     values.get("selinux", "")) is not None
                    and values.get("pid", "").isdigit() and int(values["pid"]) > 0)
        processes.append({
            "role": role, "pid": values.get("pid"), "uid": values.get("uid"),
            "euid": values.get("euid"), "capEff": values.get("cap_eff"),
            "selinux": values.get("selinux"), "ordinaryIdentityPassed": bool(ordinary),
            "executableMappings": sorted(executable), "privateReadonlyMappingsPassed": property_maps,
            "readonlyPropertyMaps": sum(properties.values()),
            "guestLibraryIsolationPassed": executable == allowed,
            "passed": bool(ordinary and property_maps and executable == allowed),
        })
    passed = (len(processes) == 2 and {p["role"] for p in processes} == {"parent", "child"}
              and len({p["pid"] for p in processes}) == 2
              and all(p["passed"] for p in processes))
    return {"passed": passed, "processes": processes}
