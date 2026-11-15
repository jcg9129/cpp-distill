# SPDX-License-Identifier: GPL-3.0-or-later
"""Every executed stub has a wall timeout, independent of the job timeout."""
import argparse
from pathlib import Path
import platform
import subprocess
import time
from common import capture, summary, write_json

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--standard", default="gnu++20")
parser.add_argument("--label", default="native")
parser.add_argument("--directory", type=Path, default=Path("isolated"))
parser.add_argument("--output", type=Path, default=Path("results/smoke.json"))
args = parser.parse_args()
binary = (args.directory / ("distill-stub.exe" if platform.system() == "Windows" else "distill-stub")).resolve()
result = dict(label=args.label, compiler=args.compiler, tests={})
failed = False
tests = [
    ("resources", [binary, "--list-resources"], True),
    ("self_check", [binary, "--self-check"], True),
    ("gcc_smoke", [binary, args.compiler, (args.directory / "smoke.cpp").resolve(), "-std=" + args.standard], True),
    ("foreign_triple", [binary, "--foreign-check", (args.directory / "smoke-portable.cpp").resolve()], False),
]
if platform.system() == "Windows":
    tests.append(("unicode_space_path", [binary, args.compiler, (args.directory / "space café/smoke test.cpp").resolve()], True))
for name, command, required in tests:
    start = time.monotonic()
    entry = dict(required=required, success=False)
    try:
        entry["output"] = capture(command, timeout=120)
        entry["success"] = True
    except subprocess.CalledProcessError as error:
        entry.update(exit_code=error.returncode, output=error.stdout)
    except Exception as error:
        entry["error"] = str(error)
    entry["wall_seconds"] = round(time.monotonic() - start, 3)
    result["tests"][name] = entry
    failed |= required and not entry["success"]
write_json(args.output, result)
summary(f"Stub smoke tests: {args.label}", result)
raise SystemExit(1 if failed else 0)
