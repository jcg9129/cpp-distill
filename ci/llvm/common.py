# SPDX-License-Identifier: GPL-3.0-or-later
"""Small shared CI helpers; no shell commands or implicit credential discovery."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request

PLATFORMS = [
    dict(platform="linux-x86_64", runner="ubuntu-24.04", container="quay.io/pypa/manylinux_2_28_x86_64", target="X86", arch="x86_64", official="LLVM-{version}-Linux-X64.tar.xz"),
    dict(platform="linux-arm64", runner="ubuntu-24.04-arm", container="quay.io/pypa/manylinux_2_28_aarch64", target="AArch64", arch="arm64", official="LLVM-{version}-Linux-ARM64.tar.xz"),
    dict(platform="macos-arm64", runner="macos-15", container=None, target="AArch64", arch="arm64", official="LLVM-{version}-macOS-ARM64.tar.xz"),
    dict(platform="macos-x86_64", runner="macos-15-intel", container=None, target="X86", arch="x86_64", official=None),
    dict(platform="windows-x86_64", runner="windows-2025", container=None, target="X86", arch="x86_64", official="clang+llvm-{version}-x86_64-pc-windows-msvc.tar.xz"),
]


def run(args, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def capture(args, **kwargs):
    return run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
               encoding="utf-8", errors="replace", **kwargs).stdout


def write_json(path, data):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def summary(title, data):
    text = f"## {title}\n\n```json\n{json.dumps(data, indent=2)}\n```\n"
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        # Inside docker run the host's summary path is not mounted; skip it there.
        try:
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
                stream.write(text)
        except OSError:
            pass
    print(text)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def release(version):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.]+)?", version):
        raise ValueError("Invalid LLVM version")
    request = urllib.request.Request(
        f"https://api.github.com/repos/llvm/llvm-project/releases/tags/llvmorg-{version}",
        headers={"Accept": "application/vnd.github+json", "User-Agent": "cpp-distill-D1"})
    with urllib.request.urlopen(request, timeout=120) as response:
        return json.load(response)


def download_asset(metadata, name, directory):
    asset = next((a for a in metadata["assets"] if a["name"] == name), None)
    if not asset:
        raise RuntimeError(f"Official release has no asset {name}")
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / name
    with urllib.request.urlopen(asset["browser_download_url"], timeout=120) as response, path.open("wb") as output:
        shutil.copyfileobj(response, output, 1024 * 1024)
    actual = sha256(path)
    expected = None
    method = "recorded only: release has no published SHA256 checksum"
    if (asset.get("digest") or "").startswith("sha256:"):
        expected = asset["digest"].split(":", 1)[1]
        method = "GitHub release asset SHA256 digest"
    else:
        # LLVM releases have used several checksum naming conventions.
        for candidate in metadata["assets"]:
            lower = candidate["name"].lower()
            if not (lower in {name.lower() + ".sha256", name.lower() + ".sha256sum", "sha256sums", "sha256sums.txt"}
                    or ("sha256" in lower and lower.endswith(".txt"))):
                continue
            with urllib.request.urlopen(candidate["browser_download_url"], timeout=120) as response:
                lines = response.read().decode("utf-8").splitlines()
            for line in lines:
                match = re.search(r"\b[0-9a-fA-F]{64}\b", line)
                if match and (line.rstrip().endswith(name) or f"({name})" in line or candidate["name"].startswith(name + ".")):
                    expected = match.group().lower()
                    method = candidate["name"]
                    break
            if expected:
                break
    if expected and expected.lower() != actual:
        raise RuntimeError(f"SHA256 mismatch for {name}")
    return path, dict(sha256=actual, verification=method, url=asset["browser_download_url"])


def extract(archive, destination):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    # data_filter rejects traversal, escaping links and special devices.
    with tarfile.open(archive, "r:*") as stream:
        stream.extractall(destination, filter="data")


def tree_bytes(path):
    return sum(p.stat().st_size for p in Path(path).rglob("*") if p.is_file() and not p.is_symlink())
