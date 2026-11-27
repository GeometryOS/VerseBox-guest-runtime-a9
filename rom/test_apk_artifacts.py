#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""APK 侧产物与审计清单的回归测试。"""

import importlib.util
import hashlib
import json
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from apk_artifacts import build_apk_artifacts


spec = importlib.util.spec_from_file_location("pack_rom", Path(__file__).with_name("pack-rom.py"))
pack_rom = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack_rom)


class ApkArtifactsTest(unittest.TestCase):
    def test_pack_and_verify_reject_unresolved_extra_preload(self):
        preload = "liba9vm-healthd-sysfs.so"
        prepared = {"guestSdk": 28, "data": [{"name": "guest-runtime-a9/test"}],
                    "files": [], "extraPreloads": [preload]}
        with self.assertRaisesRegex(pack_rom.PackError, "extraPreloads.*healthd-sysfs"):
            pack_rom.rewrite_manifest(prepared, "guest-runtime-a9/", "rom")
        with tempfile.TemporaryDirectory() as temporary:
            rom = Path(temporary) / "fixture.zip"
            properties = "\n".join((
                "schema: versebox-rom/2", "id: android9", "romVersion: 1.0.1",
                "name: Android 9", "androidVersion: 9", "apiLevel: 28",
                "description: Fixture", "inputs: versebox_inputs.json", "payloadRoot: payload/",
                "payloadBytes: 1", "payloadSha256: " + "a" * 64, "libMode: rom",
            ))
            manifest = dict(prepared, data=[{"name": "payload/test"}])
            for present in (False, True):
                if present:
                    manifest["files"] = [{"logical": preload, "name": preload, "libPath": "payload/test"}]
                with zipfile.ZipFile(rom, "w") as archive:
                    archive.writestr("versebox_properties.yaml", properties)
                    archive.writestr("versebox_inputs.json", json.dumps(manifest))
                    archive.writestr("payload/test", b"x")
                problems = pack_rom.verify_zip(rom)
                self.assertEqual(not present, any("extraPreloads" in problem for problem in problems), problems)
                if present:
                    self.assertEqual([], problems)

    def make_rom(self, root, min_app="105"):
        rom = root / "versebox-fixture-1.2.3.zip"
        properties = "\n".join((
            "schema: versebox-rom/2",
            "id: fixture",
            "romVersion: 1.2.3",
            "inputs: versebox_inputs.json",
            "payloadRoot: payload/",
            "payloadBytes: 8",
            "payloadSha256: " + "a" * 64,
            "minAppVersionCode: " + min_app,
        ))
        inputs = json.dumps({"files": [{
            "logical": "linker64",
            "name": "libguestvm-linker64.so",
            "libPath": "payload/lib/linker64",
        }]})
        with zipfile.ZipFile(rom, "w") as archive:
            archive.writestr("versebox_properties.yaml", properties)
            archive.writestr("versebox_inputs.json", inputs)
            archive.writestr("payload/lib/linker64", b"linker64")
        resident = root / "resident-native.txt"
        resident.write_text("linker64\n", encoding="utf-8")
        return rom, resident

    def test_audit_records_rom_artifact_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rom, resident = self.make_rom(root)
            result = build_apk_artifacts(str(rom), str(root), str(resident), log=lambda _: None)
            audit = json.loads(Path(result["auditIntent"]).read_text(encoding="utf-8"))
            self.assertEqual(audit["romKey"], "fixture@1.2.3")
            self.assertEqual(audit["minAppVersionCode"], 105)
            self.assertNotIn("versionPointer", audit)
            self.assertEqual(len(audit["files"]), 3)
            for item in audit["files"]:
                path = root / item["name"]
                self.assertEqual(item["bytes"], path.stat().st_size)
                self.assertEqual(item["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            with tarfile.open(result["residentTar"]) as archive:
                self.assertEqual(archive.getnames(), ["libguestvm-linker64.so"])

    def test_dexopt_profile_requires_installer_shim(self):
        manifest = {"files": [], "runtimeProfile": {"installer": True, "dexoptShim": True}}
        self.assertTrue(any("liba9vm-dexopt-shim.so" in item
                            for item in pack_rom.preload_problems(manifest)))
        manifest["files"].append({"logical": "liba9vm-dexopt-shim.so"})
        self.assertEqual([], pack_rom.preload_problems(manifest))

    def test_audit_rejects_missing_minimum_app_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            rom, resident = self.make_rom(root, min_app="0")
            with self.assertRaisesRegex(RuntimeError, "minAppVersionCode"):
                build_apk_artifacts(str(rom), str(root), str(resident), log=lambda _: None)


if __name__ == "__main__":
    unittest.main()
