# SPDX-License-Identifier: GPL-3.0-or-later
"""CI-only SDK consumer; isolation is deliberately a separate process/step."""
import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path.cwd().resolve()
WORK = ROOT / "spike-work"
BUILD = WORK / "cli-build"
RESULTS = ROOT / "results"


def build(sdk_platform):
    RESULTS.mkdir(exist_ok=True)
    subprocess.run([sys.executable, "ci/llvm/acquire.py", "--source", "release",
                    "--version", "23.1.5", "--rev", "1", "--platform", sdk_platform],
                   check=True, timeout=900)
    command = ["cmake", "-S", ".", "-B", str(BUILD), "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF",
               f"-DDISTILL_LLVM_DIR={WORK / 'sdk/lib/cmake/llvm'}"]
    if platform.system() == "Darwin":
        arch = "arm64" if sdk_platform == "macos-arm64" else "x86_64"
        command += [f"-DCMAKE_OSX_ARCHITECTURES={arch}", "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0",
                    "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"]
    elif platform.system() == "Windows":
        command += ["-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl", "-DCMAKE_RC_COMPILER=rc"]
    else:
        command += ["-DCMAKE_C_COMPILER=gcc", "-DCMAKE_CXX_COMPILER=g++"]
    for name, argv in [("configure", command),
                       ("build", ["cmake", "--build", str(BUILD), "--target", "distill",
                                  "distill_prune_worker", "--parallel", "2", "--verbose"])]:
        with (RESULTS / f"cli-{name}.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
        print((RESULTS / f"cli-{name}.log").read_text(encoding="utf-8", errors="replace"))
        result.check_returncode()


def isolate():
    isolated = ROOT / "isolated"
    isolated.mkdir()  # Must be a new directory, like the distribution spike.
    suffix = ".exe" if platform.system() == "Windows" else ""
    for name in ("distill", "distill_prune_worker"):
        shutil.move(BUILD / (name + suffix), isolated / (name + suffix))
    shutil.copy2(WORK / "acquisition.json", RESULTS / "acquisition.json")
    shutil.rmtree(WORK)
    # AlmaLinux sees only this directory: binaries, fixtures, harness and evidence.
    shutil.copytree(ROOT / "tests/cases/pipeline", isolated / "fixtures")
    shutil.copy2(ROOT / "ci/matrix/smoke.py", isolated / "smoke.py")
    (RESULTS / "isolation.json").write_text(json.dumps({
        "sdk_deleted": not WORK.exists(), "build_deleted": not BUILD.exists(),
        "binaries": [p.name for p in isolated.glob("distill*")],
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--isolate", action="store_true")
    args = parser.parse_args()
    if args.isolate:
        isolate()
    else:
        build(args.platform)
