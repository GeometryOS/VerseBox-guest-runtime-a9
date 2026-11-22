#!/usr/bin/env python3
"""maxcv 语义与审计清单结构的回归测试（ROM 侧，不依赖网络）。"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from version_gate import GateError, load_intent, parse_rom_key, select


def intent(rom_id, version, min_app, names=None):
    names = names or [
        "versebox-%s-%s.zip" % (rom_id, version),
        "versebox-%s-%s-min.zip" % (rom_id, version),
        "apk-resident-libs-%s-%s.tar" % (rom_id, version),
    ]
    return {
        "schema": "versebox-rom-pack-audit-intent/1",
        "romKey": "%s@%s" % (rom_id, version),
        "minAppVersionCode": min_app,
        "packSeconds": 1.0,
        "files": [{"name": name, "bytes": 10, "sha256": "a" * 64} for name in names],
    }


class SelectTest(unittest.TestCase):
    def test_maxcv_skips_incompatible_higher_version(self):
        entries = [intent("android10", "1.0.1", 105), intent("android10", "1.0.2", 999)]
        self.assertEqual(select(entries, 105)["romKey"], "android10@1.0.1")

    def test_maxcv_picks_highest_compatible(self):
        entries = [intent("android10", "1.0.1", 100), intent("android10", "1.0.2", 105)]
        self.assertEqual(select(entries, 105)["romKey"], "android10@1.0.2")

    def test_pinned_version_still_respects_floor(self):
        with self.assertRaises(GateError):
            select([intent("android10", "1.0.2", 999)], 105, "1.0.2")

    def test_pinned_version_ok(self):
        entries = [intent("android10", "1.0.1", 105), intent("android10", "1.0.2", 999)]
        self.assertEqual(select(entries, 105, "1.0.1")["romKey"], "android10@1.0.1")

    def test_no_compatible(self):
        with self.assertRaises(GateError):
            select([intent("android10", "1.0.1", 200)], 105)


class IntentTest(unittest.TestCase):
    def test_load_rejects_version_pointer(self):
        data = intent("android10", "1.0.1", 105)
        data["versionPointer"] = "maxcv"
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "pack-audit-intent.json"
            path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaises(GateError):
                load_intent(str(path))

    def test_load_rejects_missing_min_app(self):
        data = intent("android10", "1.0.1", 105)
        del data["minAppVersionCode"]
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "pack-audit-intent.json"
            path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaises(GateError):
                load_intent(str(path))

    def test_load_rejects_duplicate_names(self):
        data = intent("android10", "1.0.1", 105, names=["a.zip", "a.zip", "b.tar"])
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "pack-audit-intent.json"
            path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaises(GateError):
                load_intent(str(path))

    def test_parse_rom_key(self):
        self.assertEqual(parse_rom_key("android15@1.0.1"), ("android15", "1.0.1"))
        with self.assertRaises(GateError):
            parse_rom_key("android15")


if __name__ == "__main__":
    unittest.main()
