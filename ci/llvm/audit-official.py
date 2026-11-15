# SPDX-License-Identifier: GPL-3.0-or-later
"""An archive inventory alone is not proof of a usable SDK: link the real stub."""
import argparse
import importlib.util
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
from common import PLATFORMS, capture, run, summary, write_json

parser = argparse.ArgumentParser()
parser.add_argument("--platform", required=True)
args = parser.parse_args()
row = next(p for p in PLATFORMS if p["platform"] == args.platform)
work = Path("spike-work").resolve()
sdk = work / "sdk"
required = ["clangTooling", "clangFrontend", "clangDriver", "clangSema", "clangAST", "clangLex", "clangBasic", "clangSerialization", "clangParse", "clangAnalysis", "clangEdit", "clangASTMatchers", "LLVMSupport", "LLVMOption", "LLVMFrontendOpenMP", "LLVMTargetParser", "clangToolingCore", "clangToolingInclusions", "clangRewrite", "LLVMCore", "LLVMBitReader", "LLVMBinaryFormat", "LLVMObject", "LLVMRemarks", "LLVMDemangle", f"LLVM{row['target']}Info"]
result = dict(platform=args.platform, acquisition=json.loads((work / "acquisition.json").read_text()), libraries={})
libraries = []
for name in required:
    matches = list((sdk / "lib").glob(f"{name}.lib" if platform.system() == "Windows" else f"lib{name}.a"))
    result["libraries"][name] = [str(p.relative_to(sdk)) for p in matches]
    libraries.extend(matches)
result["clang_cmake_config"] = (sdk / "lib/cmake/clang/ClangConfig.cmake").is_file()
result["generated_headers"] = {name: (sdk / "include" / name).is_file() for name in
                               ["clang/Basic/Version.inc", "clang/Basic/DiagnosticSemaKinds.inc", "llvm/Config/llvm-config.h"]}
result["resource_headers"] = [str(p.relative_to(sdk)) for p in sdk.glob("lib/clang/*/include/stddef.h")]
result["library_platform_samples"] = {}
for library in libraries:
    try:
        if platform.system() == "Windows":
            # Scan directives in all members, retaining only DEFAULTLIB records.
            defaults = set()
            with subprocess.Popen(["dumpbin", "/nologo", "/directives", str(library)], stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, errors="replace") as process:
                for line in process.stdout:
                    defaults.update(re.findall(r"[/-]DEFAULTLIB:([^\s]+)", line, re.I))
                if process.wait():
                    raise RuntimeError("dumpbin failed")
            result["library_platform_samples"][library.name] = dict(
                defaultlibs=sorted(defaults),
                crt=("mixed" if any("LIBCMT" in d.upper() for d in defaults) and any("MSVCRT" in d.upper() for d in defaults)
                     else "static (LIBCMT)" if any("LIBCMT" in d.upper() for d in defaults)
                     else "dynamic (MSVCRT)" if any("MSVCRT" in d.upper() for d in defaults) else "unknown"))
        elif platform.system() == "Darwin":
            members = capture(["ar", "-t", library], timeout=60).splitlines()
            member = next(m for m in members if m.endswith(".o"))
            obj = work / "sample.o"
            with obj.open("wb") as output:
                run(["ar", "-p", library, member], stdout=output, timeout=60)
            output = capture(["otool", "-l", obj], timeout=60)
            minos = re.findall(r"\bminos\s+([\d.]+)", output)
            minos += re.findall(r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)", output)
            result["library_platform_samples"][library.name] = dict(member=member, minos=minos, note="one object sampled per required library")
    except Exception as error:
        result["library_platform_samples"][library.name] = dict(error=str(error))
run([sys.executable, "ci/llvm/build-stub.py", "--platform", args.platform, "--observe"])
result["stub_build"] = json.loads((work / "stub-build.json").read_text())
binary = work / "build" / ("distill-stub.exe" if platform.system() == "Windows" else "distill-stub")
result["linked_stub_audit"] = {"pass": False, "unavailable": "Stub configure/link failed; no binary to inspect"}
if result["stub_build"]["success"]:
    spec = importlib.util.spec_from_file_location("binary_audit", Path(__file__).with_name("binary-audit.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    try:
        result["linked_stub_audit"] = module.audit(binary)
    except Exception as error:
        result["linked_stub_audit"] = dict(error=str(error))
    try:
        result["self_check"] = capture([binary, "--self-check"], timeout=60)
    except Exception as error:
        result["self_check_error"] = str(error)
# Logs expose exact missing packages/symbols. Config exports declare external
# libraries; the actual linked NEEDED list shows which ones the stub requires.
result["optional_dependency_export_lines"] = []
for config in (sdk / "lib/cmake").rglob("*.cmake"):
    for line in config.read_text(encoding="utf-8", errors="replace").splitlines():
        if re.search(r"zlib|zstd|libxml2|terminfo|curses", line, re.I):
            result["optional_dependency_export_lines"].append(f"{config.relative_to(sdk)}: {line.strip()}")
result["usable"] = (all(result["libraries"].values()) and result["clang_cmake_config"]
                    and all(result["generated_headers"].values()) and bool(result["resource_headers"])
                    and result["stub_build"]["success"] and "self_check_error" not in result
                    and result.get("linked_stub_audit", {}).get("pass", False))
write_json("audit.json", result)
summary(f"Official archive audit: {args.platform}", result)
