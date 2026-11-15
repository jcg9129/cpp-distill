# SPDX-License-Identifier: GPL-3.0-or-later
"""Retain the old-glibc smoke result even when the container command fails."""
from pathlib import Path
import subprocess
import time
from common import run, summary, write_json

start = time.monotonic()
result = dict(success=False, compiler="AlmaLinux 8 gcc-c++", standard="gnu++17", log="almalinux8.log")
try:
    with Path("results/almalinux8.log").open("w", encoding="utf-8") as output:
        run(["bash", "ci/llvm/alma-smoke.sh"], stdout=output,
            stderr=subprocess.STDOUT, timeout=900)
    result["success"] = True
except Exception as error:
    result["error"] = str(error)
finally:
    result["wall_seconds"] = round(time.monotonic() - start, 3)
    print(Path("results/almalinux8.log").read_text(encoding="utf-8", errors="replace"))
    write_json("results/almalinux8.json", result)
    summary("AlmaLinux 8 / glibc 2.28", result)
raise SystemExit(0 if result["success"] else 1)
