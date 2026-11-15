# SPDX-License-Identifier: GPL-3.0-or-later
"""Publish with the explicit workflow token; all assets are <= 2 GiB."""
import argparse
import json
import os
from pathlib import Path
import shutil
import urllib.error
import urllib.parse
import urllib.request
from common import sha256

parser = argparse.ArgumentParser()
parser.add_argument("--version", required=True)
parser.add_argument("--rev", required=True, type=int)
args = parser.parse_args()
repository = os.environ["GITHUB_REPOSITORY"]
commit = os.environ["GITHUB_SHA"]
token = os.environ["GH_TOKEN"]
base = f"https://api.github.com/repos/{repository}"


def request(method, url, data=None):
    headers = {"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json", "User-Agent": "cpp-distill-D1"}
    if data is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(data).encode()
    try:
        with urllib.request.urlopen(urllib.request.Request(url, data=data, method=method, headers=headers), timeout=120) as response:
            return json.load(response) if response.status != 204 else None
    except urllib.error.HTTPError as error:
        # GitHub explains 403/422 refusals in the body; show it before re-raising.
        print(f"{method} {url} -> HTTP {error.code}: {error.read().decode(errors='replace')[:2000]}")
        raise


tag = f"llvm-{args.version}-{args.rev}"
try:
    existing = request("GET", base + "/releases/tags/" + tag)
except urllib.error.HTTPError as error:
    if error.code != 404:
        raise
    existing = None
# Refuse to silently retarget a previously published immutable SDK identity.
try:
    ref = request("GET", base + "/git/ref/tags/" + tag)["object"]
    while ref["type"] == "tag":
        ref = request("GET", ref["url"])["object"]
    if ref["sha"] != commit:
        raise RuntimeError(f"{tag} already points at another commit; bump sdk_rev")
except urllib.error.HTTPError as error:
    if error.code != 404:
        raise
body = dict(tag_name=tag, target_commitish=commit, name=tag,
            body="LLVM/Clang static SDKs for cpp-distill D1. Settings are keyed by sdk_rev.\n"
                 "SHA256SUMS covers every uploaded asset/part. Concatenate .partNNN in order before extraction.\n"
                 "The official LLVM source archive is included as corresponding source; see its embedded licenses.")
staging = Path("release-assets")
staging.mkdir()
for path in Path("artifacts").rglob("*"):
    if not path.is_file() or path.suffix == ".log":
        continue
    name = path.name
    if name == "measurements.json":
        platform = json.loads(path.read_text())["platform"]
        name = f"measurements-{platform}.json"
    if not (name.endswith(".tar.xz") or name.startswith("measurements-")):
        continue
    destination = staging / name
    if destination.exists():
        if sha256(destination) != sha256(path):
            raise RuntimeError(f"Conflicting assets: {name}")
        continue
    shutil.move(path, destination)
limit = 2 * 1024**3 - 1024**2
for path in list(staging.iterdir()):
    if path.stat().st_size <= limit:
        continue
    with path.open("rb") as source:
        index = 0
        while True:
            first = source.read(1024 * 1024)
            if not first:
                break
            with Path(str(path) + f".part{index:03}").open("wb") as part:
                part.write(first)
                remaining = limit - len(first)
                while remaining:
                    chunk = source.read(min(1024 * 1024, remaining))
                    if not chunk:
                        break
                    part.write(chunk)
                    remaining -= len(chunk)
            index += 1
    path.unlink()
if not any(p.name.startswith("llvm-project-") for p in staging.iterdir()):
    raise RuntimeError("Missing corresponding source artifact")
(staging / "SHA256SUMS").write_text("".join(f"{sha256(p)}  {p.name}\n" for p in sorted(staging.iterdir())), encoding="utf-8")
release = request("PATCH", existing["url"], body) if existing else request("POST", base + "/releases", body)
# Keep assets for unselected platforms on an existing release, and preserve their
# checksums too, so a partial rebuild does not break earlier platform downloads.
assets = []
page = 1
while True:
    batch = request("GET", release["assets_url"] + f"?per_page=100&page={page}")
    assets += batch
    if len(batch) < 100:
        break
    page += 1
old_sums = next((a for a in assets if a["name"] == "SHA256SUMS"), None)
if old_sums:
    with urllib.request.urlopen(old_sums["browser_download_url"], timeout=120) as response:
        old_lines = response.read().decode().splitlines()
    current = {p.name for p in staging.iterdir()}
    retained = [line for line in old_lines if line.split(maxsplit=1)[1].lstrip("*") not in current]
    with (staging / "SHA256SUMS").open("a", encoding="utf-8") as output:
        output.write("".join(line + "\n" for line in retained))
for path in sorted(staging.iterdir()):
    for old in assets:
        if old["name"] == path.name:
            request("DELETE", old["url"])
    url = release["upload_url"].split("{", 1)[0] + "?name=" + urllib.parse.quote(path.name)
    # http.client streams file objects; Content-Length avoids chunked uploads.
    with path.open("rb") as stream:
        headers = {"Authorization": f"Bearer {token}", "Content-Type": "application/octet-stream",
                   "Content-Length": str(path.stat().st_size), "User-Agent": "cpp-distill-D1"}
        with urllib.request.urlopen(urllib.request.Request(url, data=stream, headers=headers, method="POST"), timeout=600) as response:
            json.load(response)
    print(f"Published {path.name}")
