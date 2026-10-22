#!/usr/bin/env python3
"""Download the Redroid Android 9 arm64 image without Docker.

The layer blobs come from the Docker Registry HTTP API. Downloads resume from a
.part file and are verified against the registry's sha256 digest.
"""
import hashlib
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = "redroid/redroid"
TAG = os.environ.get("REDROID_TAG", "9.0.0-240527")
ARCH = "arm64"
DOWNLOAD = ROOT / "downloads"
AUTH = "https://auth.docker.io/token?service=registry.docker.io&scope=repository:{repo}:pull"
REGISTRY = "https://registry-1.docker.io/v2/{repo}"


def token():
    with urllib.request.urlopen(AUTH.format(repo=REPO), timeout=60) as response:
        return json.load(response)["token"]


def get_json(url, bearer, accept):
    request = urllib.request.Request(url, headers={
        "Authorization": "Bearer " + bearer,
        "Accept": accept,
    })
    with urllib.request.urlopen(request, timeout=120) as response:
        return json.load(response)


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 << 20), b""):
            value.update(block)
    return value.hexdigest()


def download(url, bearer, target, size, workers=8):
    """Download one blob with parallel HTTP Range requests."""
    partial = target.with_suffix(target.suffix + ".part")
    partial.unlink(missing_ok=True)
    with partial.open("wb") as stream:
        stream.truncate(size)
    chunks = []
    span = (size + workers - 1) // workers
    for index in range(workers):
        start = index * span
        if start >= size:
            break
        chunks.append([start, min(start + span, size) - 1])
    lock = threading.Lock()
    written = [0]
    errors = []
    started = time.monotonic()
    last_report = [started]

    def worker(start, end, number):
        position = start
        tries = 0
        while position <= end:
            try:
                headers = {"Authorization": "Bearer " + bearer,
                           "Range": "bytes=%d-%d" % (position, end)}
                request = urllib.request.Request(url, headers=headers)
                with urllib.request.urlopen(request, timeout=120) as response:
                    if response.status != 206:
                        raise RuntimeError("range request ignored: HTTP %d" % response.status)
                    handle = open(partial, "r+b")
                    try:
                        while position <= end:
                            block = response.read(min(1 << 20, end - position + 1))
                            if not block:
                                raise RuntimeError("truncated range at %d" % position)
                            handle.seek(position)
                            handle.write(block)
                            position += len(block)
                            with lock:
                                written[0] += len(block)
                                now = time.monotonic()
                                if now - last_report[0] >= 5:
                                    rate = written[0] / (now - started) / (1 << 20)
                                    print("  %5.1f%%  %.1f/%.1f MiB  %.1f MiB/s" % (
                                        written[0] * 100.0 / size, written[0] / (1 << 20),
                                        size / (1 << 20), rate), flush=True)
                                    last_report[0] = now
                    finally:
                        handle.close()
                tries = 0
            except Exception as failure:
                tries += 1
                if tries > 6:
                    with lock:
                        errors.append("chunk %d at %d: %s" % (number, position, failure))
                    return
                time.sleep(2 * tries)

    threads = [threading.Thread(target=worker, args=(start, end, number), daemon=True)
               for number, (start, end) in enumerate(chunks)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    if errors:
        raise RuntimeError("; ".join(errors))
    if partial.stat().st_size != size:
        raise RuntimeError("size mismatch: %d != %d" % (partial.stat().st_size, size))
    os.replace(partial, target)


def main():
    DOWNLOAD.mkdir(parents=True, exist_ok=True)
    bearer = token()
    index = get_json(
        REGISTRY.format(repo=REPO) + "/manifests/" + TAG, bearer,
        "application/vnd.oci.image.index.v1+json, "
        "application/vnd.docker.distribution.manifest.list.v2+json, "
        "application/vnd.oci.image.manifest.v1+json, "
        "application/vnd.docker.distribution.manifest.v2+json")
    if "manifests" in index:
        matches = [item for item in index["manifests"]
                   if item.get("platform", {}).get("architecture") == ARCH
                   and item.get("platform", {}).get("os") == "linux"]
        if not matches:
            raise SystemExit("no linux/%s manifest in %s" % (ARCH, TAG))
        manifest_digest = matches[0]["digest"]
    else:
        manifest_digest = None
    manifest = get_json(
        REGISTRY.format(repo=REPO) + "/manifests/" + (manifest_digest or TAG), bearer,
        "application/vnd.oci.image.manifest.v1+json, "
        "application/vnd.docker.distribution.manifest.v2+json")
    layers = manifest["layers"]
    print("tag=%s arch=%s layers=%d compressed=%.1f MiB" % (
        TAG, ARCH, len(layers), sum(layer["size"] for layer in layers) / (1 << 20)), flush=True)
    records = []
    for number, layer in enumerate(layers):
        media = layer["mediaType"]
        if not media.endswith("tar+gzip") and not media.endswith("tar"):
            raise SystemExit("unsupported layer media type: " + media)
        suffix = ".tar.gz" if media.endswith("gzip") else ".tar"
        target = DOWNLOAD / ("%s-%d%s" % (TAG, number, suffix))
        print("layer %d %s %d bytes" % (number, layer["digest"], layer["size"]), flush=True)
        if not target.is_file() or digest(target) != layer["digest"].split(":")[1]:
            download(REGISTRY.format(repo=REPO) + "/blobs/" + layer["digest"], bearer,
                     target, layer["size"])
        actual = digest(target)
        expected = layer["digest"].split(":")[1]
        if actual != expected:
            raise SystemExit("layer digest mismatch: %s != %s" % (actual, expected))
        records.append({"path": str(target), "sha256": actual, "bytes": target.stat().st_size,
                        "mediaType": media})
        print("verified " + target.name, flush=True)
    (DOWNLOAD / "manifest.json").write_text(json.dumps({
        "repository": REPO, "tag": TAG, "architecture": ARCH,
        "manifestDigest": manifest_digest, "layers": records,
    }, indent=2) + "\n", encoding="utf-8")
    print("done", flush=True)


if __name__ == "__main__":
    main()
