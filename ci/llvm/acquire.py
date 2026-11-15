# SPDX-License-Identifier: GPL-3.0-or-later
"""Download/extract an SDK (workflow artifacts are downloaded by the action)."""
import argparse
import json
import os
from pathlib import Path
import shutil
import urllib.request
from common import PLATFORMS, download_asset, extract, release, sha256, write_json

parser = argparse.ArgumentParser()
parser.add_argument("--source", choices=["official", "release", "run"], required=True)
parser.add_argument("--version", required=True)
parser.add_argument("--rev", default="1")
parser.add_argument("--platform", required=True)
args = parser.parse_args()
work = Path("spike-work").resolve()
work.mkdir(exist_ok=True)
downloads = work / "downloads"
info = dict(source=args.source, platform=args.platform)
if args.source == "official":
    row = next(p for p in PLATFORMS if p["platform"] == args.platform)
    if not row["official"]:
        raise RuntimeError("No official archive for this platform")
    archive, verification = download_asset(release(args.version), row["official"].format(version=args.version), downloads)
    info.update(verification)
elif args.source == "release":
    name = f"llvm-sdk-{args.version}-{args.rev}-{args.platform}.tar.xz"
    url = f"https://github.com/{os.environ['GITHUB_REPOSITORY']}/releases/download/llvm-{args.version}-{args.rev}"
    downloads.mkdir(exist_ok=True)
    with urllib.request.urlopen(url + "/SHA256SUMS", timeout=120) as response:
        sums = dict((line.split(maxsplit=1)[1].lstrip("*"), line.split()[0]) for line in response.read().decode().splitlines() if line.strip())
    names = [name] if name in sums else sorted(key for key in sums if key.startswith(name + ".part"))
    if not names:
        raise RuntimeError(f"No {name} or split parts in SHA256SUMS")
    paths = []
    for part in names:
        path = downloads / part
        with urllib.request.urlopen(url + "/" + part, timeout=120) as response, path.open("wb") as output:
            shutil.copyfileobj(response, output, 1024 * 1024)
        if sha256(path) != sums[part]:
            raise RuntimeError(f"SHA256 mismatch: {part}")
        paths.append(path)
    archive = downloads / name
    if paths != [archive]:
        with archive.open("wb") as output:
            for path in paths:
                with path.open("rb") as stream:
                    shutil.copyfileobj(stream, output, 1024 * 1024)
                path.unlink()
    info["sha256"] = sha256(archive)
    info["verification"] = "release SHA256SUMS (including every split part)"
else:
    matches = list(Path("sdk-artifact").rglob("*.tar.xz"))
    if len(matches) != 1:
        raise RuntimeError(f"Expected exactly one SDK artifact archive, found {matches}")
    archive = matches[0]
    info["sha256"] = sha256(archive)
    info["verification"] = "GitHub Actions artifact transport; recorded local SHA256"
extract(archive, work / "extracted")
extracted = work / "extracted"
if (extracted / "lib").is_dir() and (extracted / "include").is_dir():
    root = extracted  # Also accept an official archive without a wrapper folder.
else:
    roots = [p for p in extracted.iterdir() if p.is_dir()]
    if len(roots) != 1:
        raise RuntimeError(f"Expected one archive root: {roots}")
    root = roots[0]
shutil.move(str(root), work / "sdk")
write_json(work / "acquisition.json", info)
# Extraction temporarily doubles disk use; these archives are not needed again.
if args.source != "run":
    shutil.rmtree(downloads)
else:
    shutil.rmtree("sdk-artifact")
