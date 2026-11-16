# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only release preparation, provenance, packaging and checksum gates."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
LLVM_REVISION = "llvmorg-23.1.5"
SDK_RELEASE = "llvm-23.1.5-1"
LLVM_SOURCE = "llvm-project-23.1.5.src.tar.xz"
PLATFORMS = {"linux-x86_64", "linux-arm64", "macos-arm64", "macos-x86_64", "windows-x86_64"}
DOCUMENTS = ("LICENSE", "README.md", "THIRD_PARTY_NOTICES.md", "LLVM-LICENSE.txt")
OPTIONAL = ("LLVM_ENABLE_ZLIB", "LLVM_ENABLE_ZSTD", "LLVM_ENABLE_LIBXML2", "CLANG_ENABLE_LIBXML2")


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def save(path, value):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def identity(ref_type, ref_name, event, dry_run, commit):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected a full distill commit SHA")
    tag = ref_name if ref_type == "tag" else None
    if tag:
        if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?", tag):
            raise ValueError("Release tags must be v<major>.<minor>.<patch> with optional prerelease/build suffix")
        version = tag[1:]
    elif event == "workflow_dispatch" and dry_run:
        version = "0.0.0-dev+" + commit[:12]
    else:
        raise ValueError("Publishing requires an existing v* tag; branch dispatch requires dry_run=true")
    return dict(version=version, commit=commit, tag=tag, dry_run=dry_run,
                llvm_revision=LLVM_REVISION, sdk_release=SDK_RELEASE)


def prepare():
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True, timeout=30).strip()
    event = os.environ["GITHUB_EVENT_NAME"]
    dry_run = event == "workflow_dispatch" and os.environ.get("DRY_RUN", "true") == "true"
    value = identity(os.environ["GITHUB_REF_TYPE"], os.environ["GITHUB_REF_NAME"], event, dry_run, commit)
    save("results/release.json", value)
    print(json.dumps(value, indent=2))


def checksums(text):
    result = {}
    for line in text.splitlines():
        digest, name = line.split(maxsplit=1)
        name = name.lstrip("*")
        if not re.fullmatch(r"[a-f0-9]{64}", digest) or Path(name).name != name or name in result:
            raise ValueError("Invalid or duplicate SHA256SUMS entry")
        result[name] = digest
    return result


def download(url):
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def sdk_evidence(repository, sdk_platform, acquisition):
    base = f"https://github.com/{repository}/releases/download/{SDK_RELEASE}"
    sums = checksums(download(base + "/SHA256SUMS").decode())
    name = f"measurements-{sdk_platform}.json"
    data = download(base + "/" + name)
    if hashlib.sha256(data).hexdigest() != sums[name]:
        raise ValueError("SDK measurements checksum mismatch")
    measurements = json.loads(data)
    if (measurements["status"] != "success" or measurements["platform"] != sdk_platform or
            measurements["llvm_version"] != "23.1.5" or measurements["sdk_rev"] != 1):
        raise ValueError("SDK measurements have the wrong identity or failed build")
    if measurements["source_archive"]["sha256"] != sums[LLVM_SOURCE]:
        raise ValueError("SDK source differs from the corresponding-source release asset")
    for setting in OPTIONAL:
        if measurements["cmake_settings"][setting] != "OFF":
            raise ValueError(f"Unexpected SDK dependency {setting}; update notices before release")
    if acquisition["source"] != "release" or acquisition["platform"] != sdk_platform:
        raise ValueError("Release must consume the published platform SDK")
    archive = f"llvm-sdk-23.1.5-1-{sdk_platform}.tar.xz"
    if archive in sums:
        if acquisition["sha256"] != sums[archive]:
            raise ValueError("SDK archive changed since acquisition")
    elif not any(name.startswith(archive + ".part") for name in sums):
        raise ValueError("Missing SDK archive/parts in release checksums")
    return dict(release=SDK_RELEASE, archive_sha256=acquisition["sha256"],
                verification=acquisition["verification"], source_archive=LLVM_SOURCE,
                source_sha256=sums[LLVM_SOURCE], measurements_sha256=sums[name],
                cmake_settings=measurements["cmake_settings"])


def verify_licenses(root=ROOT):
    manifest = json.loads((root / "ci/release/licenses.json").read_text(encoding="utf-8"))
    for name, digest in manifest.items():
        if sha256(root / name) != digest:
            raise ValueError(f"License/notice checksum mismatch: {name}; expected committed bytes "
                             "(check checkout line endings and .gitattributes)")


def check_link_commands(commands):
    # Static dependencies would escape the dynamic-dependency audit. Inspect
    # Ninja's actual commands too, including absolute .a/.lib paths.
    library = r"(?:z|zlib[^ /\\\"]*|zstd[^ /\\\"]*|xml2|libxml2)"
    pattern = (r"-l(?:z|zlib(?:static)?|zstd|xml2)\b|"
               r"(?:^|[\s\"'/\\:])(?:lib)?" + library + r"\.(?:a|lib)\b")
    if re.search(pattern, commands, re.I):
        raise ValueError("Unexpected compression/XML library in link commands; update notices")


def record(build, sdk_platform):
    """Called before isolation deletes the SDK, cache and generated Ninja files."""
    verify_licenses()
    value = json.loads(Path("results/release.json").read_text(encoding="utf-8"))
    acquisition = json.loads(Path("spike-work/acquisition.json").read_text(encoding="utf-8"))
    value["sdk"] = sdk_evidence(os.environ["GITHUB_REPOSITORY"], sdk_platform, acquisition)
    value["platform"] = sdk_platform
    value["build_date"] = datetime.now(timezone.utc).isoformat()
    commands = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", "distill"],
                                       text=True, encoding="utf-8", errors="replace", timeout=120)
    check_link_commands(commands)
    Path("results/link-commands.txt").write_text(commands, encoding="utf-8")
    toolchain = (build / "distill-toolchain.txt").read_text(encoding="utf-8")
    # Keep the CLI compile and final link lines in the distributed provenance.
    cli_commands = "\n".join(line for line in commands.splitlines()
                              if "CMakeFiles/distill.dir/" in line or "CMakeFiles\\distill.dir\\" in line)
    if not cli_commands:
        raise ValueError("Missing CLI compile/link command evidence")
    info = (f"distill version: {value['version']}\ndistill commit: {value['commit']}\n"
            f"LLVM revision: {value['llvm_revision']} (unmodified release source)\n"
            f"SDK release: {SDK_RELEASE}\nSDK archive SHA256: {value['sdk']['archive_sha256']}\n"
            f"LLVM source: {LLVM_SOURCE}\nLLVM source SHA256: {value['sdk']['source_sha256']}\n"
            f"Platform: {sdk_platform}\nBuild date (UTC): {value['build_date']}\n\n"
            + toolchain + "\nCLI compile and link commands:\n" + cli_commands + "\n")
    Path("results/BUILDINFO.txt").write_text(info, encoding="utf-8")
    save("results/release.json", value)


def version_line(value):
    return f"distill {value['version']} (commit {value['commit']}; LLVM {value['llvm_revision']})\n"


def package(sdk_platform, root=ROOT):
    if sdk_platform not in PLATFORMS:
        raise ValueError("Unknown platform")
    verify_licenses(root)
    value = json.loads((root / "results/release.json").read_text(encoding="utf-8"))
    if value["platform"] != sdk_platform:
        raise ValueError("Package platform differs from build provenance")
    binary = root / "isolated" / ("distill.exe" if sdk_platform.startswith("windows-") else "distill")
    result = subprocess.run([str(binary), "--version"], check=True, capture_output=True,
                            text=True, encoding="utf-8", timeout=120)
    if result.stdout != version_line(value) or result.stderr:
        raise ValueError("Moved binary --version differs from release provenance")
    stem = f"distill-{value['version']}-{sdk_platform}"
    destination = root / "release-assets"
    destination.mkdir(exist_ok=True)
    with_stage = root / "results" / stem
    with_stage.mkdir()
    try:
        # Binary copies preserve committed LF document bytes on Windows too.
        for path in [binary, *(root / name for name in DOCUMENTS), root / "results/BUILDINFO.txt"]:
            shutil.copy2(path, with_stage / path.name)
        if sdk_platform.startswith("windows-"):
            archive = destination / (stem + ".zip")
            with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as stream:
                for path in sorted(with_stage.iterdir()):
                    stream.write(path, f"{stem}/{path.name}")
        else:
            archive = destination / (stem + ".tar.gz")
            with tarfile.open(archive, "w:gz") as stream:
                stream.add(with_stage, arcname=stem)
    finally:
        shutil.rmtree(with_stage)
    shutil.copy2(root / "results/release.json", destination / f"{sdk_platform}.json")
    print(archive)


def collect(directory=Path("release-assets"), repository=None):
    values = [json.loads((directory / f"{p}.json").read_text(encoding="utf-8")) for p in sorted(PLATFORMS)]
    first = values[0]
    for sdk_platform, value in zip(sorted(PLATFORMS), values):
        if value["platform"] != sdk_platform:
            raise ValueError("Platform provenance filename/content mismatch")
        for field in ("version", "commit", "tag", "dry_run", "llvm_revision", "sdk_release"):
            if value[field] != first[field]:
                raise ValueError(f"Platforms disagree on {field}")
        if value["sdk"]["source_sha256"] != first["sdk"]["source_sha256"]:
            raise ValueError("Platforms were built from different LLVM sources")
    expected = {f"distill-{first['version']}-{p}" + (".zip" if p.startswith("windows-") else ".tar.gz")
                for p in PLATFORMS}
    archives = sorted([*directory.glob("*.tar.gz"), *directory.glob("*.zip")])
    if {p.name for p in archives} != expected:
        raise ValueError("Expected exactly the five platform archives")
    (directory / "SHA256SUMS").write_text("".join(f"{sha256(p)}  {p.name}\n" for p in archives), encoding="utf-8")
    source_ref = first["tag"] or first["commit"]
    source = f"https://github.com/{repository}/archive/{source_ref}.tar.gz"
    llvm_source = f"https://github.com/{repository}/releases/download/{SDK_RELEASE}/{LLVM_SOURCE}"
    notes = (f"distill {first['version']} ({first['commit']}).\n\n"
             f"LLVM revision: `{LLVM_REVISION}`; SDK release: `{SDK_RELEASE}`.\n\n"
             "GPL-3.0-or-later corresponding source for these binaries is supplied by "
             f"[the distill source archive]({source}) (including build scripts) and "
             f"[the unmodified llvm-project source archive]({llvm_source}). "
             "Both are available alongside their respective releases. Runtime libraries use "
             "the exceptions/system-library terms described in THIRD_PARTY_NOTICES.md.\n\n"
             f"LLVM source SHA256: `{first['sdk']['source_sha256']}`.\n\n"
             "Verify archives with SHA256SUMS; see README.md for installation, GCC requirements "
             "and verification limits. macOS minos 14.0 is audited on macOS 15; "
             "execution on macOS 14 has not been tested.\n")
    (directory / "RELEASE-NOTES.md").write_text(notes, encoding="utf-8")
    return first


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=["prepare", "package", "collect"])
    parser.add_argument("--platform", choices=sorted(PLATFORMS))
    args = parser.parse_args()
    if args.phase == "prepare":
        prepare()
    elif args.phase == "package":
        if not args.platform:
            parser.error("package requires --platform")
        package(args.platform)
    else:
        collect(repository=os.environ["GITHUB_REPOSITORY"])
