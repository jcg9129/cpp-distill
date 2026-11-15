# SPDX-License-Identifier: GPL-3.0-or-later
"""Reuse precisely the same consumer for custom and official SDKs."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import time
from common import PLATFORMS, run, write_json

parser = argparse.ArgumentParser()
parser.add_argument("--platform", required=True)
parser.add_argument("--observe", action="store_true", help="Record official SDK failures without aborting the audit")
args = parser.parse_args()
row = next(p for p in PLATFORMS if p["platform"] == args.platform)
work = Path("spike-work").resolve()
result = dict(platform=args.platform, success=False)
start = time.monotonic()
try:
    command = ["cmake", "-S", "spike", "-B", work / "build", "-G", "Ninja",
               f"-DLLVM_SDK={work / 'sdk'}", "-DCMAKE_BUILD_TYPE=Release"]
    if platform.system() == "Darwin":
        command += [f"-DCMAKE_OSX_ARCHITECTURES={row['arch']}", "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0"]
    for stage, command in [("configure", command), ("link", ["cmake", "--build", work / "build", "--parallel", str(os.cpu_count()), "--verbose"])]:
        result["stage"] = stage
        with (work / f"stub-{stage}.log").open("w", encoding="utf-8") as log:
            run(command, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
        print((work / f"stub-{stage}.log").read_text(encoding="utf-8", errors="replace"))
    result["success"] = True
except Exception as error:
    result["error"] = str(error)
    for log in work.glob("stub-*.log"):
        print(log.read_text(encoding="utf-8", errors="replace"))
    if not args.observe:
        raise
finally:
    result["wall_seconds"] = round(time.monotonic() - start, 3)
    write_json(work / "stub-build.json", result)
