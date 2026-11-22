"""ROM 发布侧的最大兼容版本（maxcv）语义闸门。

maxcv 是 **app 侧**的选包策略：客户端只声明自己的 app versionCode，要求“当前能用的最高
ROM 版本”。ROM 仓库不声明自己要被怎么选，只发布事实：romKey、minAppVersionCode、files。

本模块测试的是“发布的事实是否足以让 maxcv 客户端做出唯一、可复现的选择”。
"""
import json
import re
import sys

SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


class GateError(Exception):
    """发布事实不合法，或无法支撑 maxcv 选择。"""


def parse_rom_key(rom_key):
    if not isinstance(rom_key, str) or "@" not in rom_key:
        raise GateError("romKey 必须是 <id>@<version>：%r" % (rom_key,))
    rom_id, version = rom_key.split("@", 1)
    if not rom_id:
        raise GateError("romKey 缺少 id：%r" % (rom_key,))
    if not SEMVER.match(version):
        raise GateError("romKey 的版本不是 MAJOR.MINOR.PATCH：%r" % (rom_key,))
    return rom_id, version


def version_tuple(version):
    return tuple(int(part) for part in version.split("."))


def load_intent(path):
    with open(path, "r", encoding="utf-8") as handle:
        try:
            data = json.load(handle)
        except json.JSONDecodeError as failure:
            raise GateError("不是合法 JSON：%s（%s）" % (path, failure))
    if data.get("schema") != "versebox-rom-pack-audit-intent/1":
        raise GateError("schema 不是 versebox-rom-pack-audit-intent/1：%s" % path)
    if "versionPointer" in data:
        raise GateError("审计清单不得声明 versionPointer（maxcv 属于 app 侧策略）：%s" % path)
    parse_rom_key(data.get("romKey"))
    min_app = data.get("minAppVersionCode")
    if not isinstance(min_app, int) or isinstance(min_app, bool) or min_app <= 0:
        raise GateError("minAppVersionCode 必须是正整数：%s" % path)
    files = data.get("files")
    if not isinstance(files, list) or len(files) < 2:
        raise GateError("files 至少要有完整包与占位包：%s" % path)
    names = set()
    for item in files:
        if not isinstance(item, dict):
            raise GateError("files 条目必须是对象：%s" % path)
        name = item.get("name")
        if not isinstance(name, str) or not name:
            raise GateError("files 条目缺少 name：%s" % path)
        if name in names:
            raise GateError("files 里有重名条目：%s" % name)
        names.add(name)
        size = item.get("bytes")
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            raise GateError("files 条目 bytes 必须为正整数：%s" % name)
        if not SHA256.match(str(item.get("sha256", ""))):
            raise GateError("files 条目 sha256 不合法：%s" % name)
    return data


def select(intents, app_version_code, version="maxcv"):
    """按 app 侧策略选包；选择必须唯一。"""
    if not isinstance(app_version_code, int) or app_version_code <= 0:
        raise GateError("app versionCode 必须是正整数")
    compatible = [item for item in intents if item["minAppVersionCode"] <= app_version_code]
    if version == "maxcv":
        if not compatible:
            raise GateError("没有任何 ROM 版本兼容 app versionCode=%d" % app_version_code)
        return max(compatible, key=lambda item: version_tuple(parse_rom_key(item["romKey"])[1]))
    matches = [item for item in compatible if parse_rom_key(item["romKey"])[1] == version]
    if not matches:
        raise GateError(
            "没有匹配 VERSION=%s 且兼容 app versionCode=%d 的版本" % (version, app_version_code)
        )
    if len(matches) > 1:
        raise GateError("VERSION=%s 命中多条，发布事实不唯一" % version)
    return matches[0]


def check_dist(dist_dir):
    """校验 dist/<romKey>/pack-audit-intent.json 的组合是否自洽。"""
    import glob
    import os

    paths = sorted(glob.glob(os.path.join(dist_dir, "*", "pack-audit-intent.json")))
    if not paths:
        print("未发现已打包产物（dist/*/pack-audit-intent.json），跳过实物校验")
        return 0
    intents = [load_intent(path) for path in paths]
    keys = [item["romKey"] for item in intents]
    if len(keys) != len(set(keys)):
        raise GateError("dist 里出现重复 romKey：%s" % keys)
    for item in intents:
        floor = item["minAppVersionCode"]
        chosen = select(intents, floor)
        if chosen["romKey"] != item["romKey"]:
            print("  %s 在 versionCode=%d 时不是最高可用版本（会选 %s），合法"
                  % (item["romKey"], floor, chosen["romKey"]))
        else:
            print("  %s 在 versionCode=%d 时可选" % (item["romKey"], floor))
        if floor > 1:
            lower = [other for other in intents if other["minAppVersionCode"] <= floor - 1]
            if any(other["romKey"] == item["romKey"] for other in lower):
                raise GateError("%s 的兼容下限没有生效" % item["romKey"])
            print("  %s 在 versionCode=%d 时被正确拒绝" % (item["romKey"], floor - 1))
    print("dist 校验通过：%d 个版本，%s" % (len(intents), ", ".join(keys)))
    return 0


def main(argv):
    args = list(argv[1:])
    if len(args) == 2 and args[0] == "--check-dist":
        try:
            return check_dist(args[1])
        except GateError as failure:
            print("版本闸门失败：%s" % failure)
            return 1
    print("用法：python rom/version_gate.py --check-dist <dist 目录>")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
