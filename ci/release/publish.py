# SPDX-License-Identifier: GPL-3.0-or-later
"""The only release mutator. Workflow dry runs skip this job entirely."""
import json
import os
from pathlib import Path
import urllib.error
import urllib.parse
import urllib.request
from release import checksums, sha256


def request(method, url, token, data=None, content_type="application/json"):
    headers = {"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json",
               "User-Agent": "cpp-distill-release", "X-GitHub-Api-Version": "2022-11-28"}
    if data is not None:
        headers["Content-Type"] = content_type
        if content_type == "application/json":
            data = json.dumps(data).encode()
        else:
            headers["Content-Length"] = str(os.fstat(data.fileno()).st_size)
    try:
        with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers, method=method),
                                    timeout=120) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        print(f"{method} {url} -> HTTP {error.code}: {error.read().decode(errors='replace')}", flush=True)
        raise


def publish(directory=Path("release-assets")):
    value = json.loads((directory / "linux-x86_64.json").read_text(encoding="utf-8"))
    if value["dry_run"] or not value["tag"]:
        raise ValueError("Refusing to publish a dry run or a branch build")
    if os.environ["GITHUB_REF"] != "refs/tags/" + value["tag"]:
        raise ValueError("Publishing ref differs from packaged tag")
    token = os.environ["GH_TOKEN"]  # Explicit job token only; no credential discovery.
    base = f"https://api.github.com/repos/{os.environ['GITHUB_REPOSITORY']}"
    # Validate the existing tag and dereference annotated tags; never create tags.
    ref = request("GET", base + "/git/ref/tags/" + urllib.parse.quote(value["tag"], safe=""), token)["object"]
    while ref["type"] == "tag":
        ref = request("GET", ref["url"], token)["object"]
    if ref["type"] != "commit" or ref["sha"] != value["commit"]:
        raise ValueError("Tag no longer points at the packaged commit")
    sums = checksums((directory / "SHA256SUMS").read_text(encoding="utf-8"))
    for name, digest in sums.items():
        if sha256(directory / name) != digest:
            raise ValueError(f"Archive checksum changed: {name}")
    body = dict(tag_name=value["tag"], target_commitish=value["commit"], name=value["tag"],
                body=(directory / "RELEASE-NOTES.md").read_text(encoding="utf-8"), draft=True,
                prerelease="-" in value["version"].split("+", 1)[0])
    # POST fails on an existing release: never delete or replace published assets.
    release = request("POST", base + "/releases", token, body)
    upload_url = release["upload_url"].split("{", 1)[0]
    for name in [*sorted(sums), "SHA256SUMS"]:
        with (directory / name).open("rb") as stream:
            request("POST", upload_url + "?name=" + urllib.parse.quote(name), token, stream,
                    "application/octet-stream")
    request("PATCH", release["url"], token, {"draft": False})


if __name__ == "__main__":
    publish()
