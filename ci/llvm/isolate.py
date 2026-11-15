# SPDX-License-Identifier: GPL-3.0-or-later
"""Move the executable to an empty directory, preserve evidence, remove the SDK."""
from pathlib import Path
import platform
import shutil
from common import sha256, write_json

work = Path("spike-work")
isolated = Path("isolated")
if isolated.exists():
    raise RuntimeError("isolation directory must be new and empty")
isolated.mkdir()
results = Path("results")
results.mkdir(exist_ok=True)
for source in list(work.glob("*.json")) + list(work.glob("*.log")):
    shutil.copy2(source, results / source.name)
name = "distill-stub.exe" if platform.system() == "Windows" else "distill-stub"
shutil.move(work / "build" / name, isolated / name)
# Delete rather than hide either path; the fresh process must use only the VFS.
shutil.rmtree(work / "sdk")
shutil.rmtree(work / "build")
shutil.rmtree(work / "extracted", ignore_errors=True)
shutil.copy2("spike/tests/smoke.cpp", isolated / "smoke.cpp")
shutil.copy2("spike/tests/smoke-portable.cpp", isolated / "smoke-portable.cpp")
space = isolated / "space café"
space.mkdir()
shutil.copy2("spike/tests/smoke.cpp", space / "smoke test.cpp")
write_json(results / "isolation.json", dict(binary_bytes=(isolated / name).stat().st_size,
                                           binary_sha256=sha256(isolated / name),
                                           sdk_deleted=not (work / "sdk").exists(),
                                           build_deleted=not (work / "build").exists()))
