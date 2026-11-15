# SPDX-License-Identifier: GPL-3.0-or-later
"""Dependency/deployment gates, also importable for observational official audits."""
import argparse
import platform
from pathlib import Path
import re
from common import capture, summary, write_json


def version_tuple(value):
    return tuple(map(int, value.split(".")))


def audit(binary):
    result = dict(binary=str(binary), platform=platform.system(), violations=[])
    if platform.system() == "Linux":
        dynamic = capture(["readelf", "-d", binary], timeout=60)
        versions = capture(["objdump", "-T", binary], timeout=60)
        needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)
        allowed = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1"}
        unexpected = [n for n in needed if n not in allowed and not re.fullmatch(r"ld-linux-[\w-]+\.so\.\d+", n)]
        glibc = sorted(set(re.findall(r"GLIBC_(\d+\.\d+(?:\.\d+)?)", versions)), key=version_tuple)
        result.update(needed=needed, glibc_versions=glibc, highest_glibc=glibc[-1] if glibc else None)
        if unexpected:
            result["violations"].append(f"Unexpected NEEDED libraries: {unexpected}")
        if not glibc:
            result["violations"].append("No GLIBC symbol versions found")
        elif version_tuple(glibc[-1]) > (2, 28):
            result["violations"].append(f"GLIBC_{glibc[-1]} exceeds 2.28")
    elif platform.system() == "Darwin":
        libraries = capture(["otool", "-L", binary], timeout=60)
        needed = [line.strip().split(" (", 1)[0] for line in libraries.splitlines()[1:] if line.strip()]
        allowed = {"/usr/lib/libSystem.B.dylib", "/usr/lib/libc++.1.dylib", "/usr/lib/libc++abi.dylib"}
        loads = capture(["otool", "-l", binary], timeout=60)
        versions = re.findall(r"\bminos\s+(\d+(?:\.\d+)+)", loads)
        versions += re.findall(r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version (\d+(?:\.\d+)+)", loads)
        result.update(needed=needed, minos=versions)
        if set(needed) - allowed:
            result["violations"].append(f"Unexpected dylibs: {sorted(set(needed) - allowed)}")
        if not versions or any(version_tuple(v)[:2] != (14, 0) for v in versions):
            result["violations"].append(f"Expected macOS minos 14.0, got {versions}")
    else:
        directives = capture(["dumpbin", "/nologo", "/dependents", binary], timeout=60)
        needed = sorted(set(re.findall(r"^\s+([\w.-]+\.dll)\s*$", directives, re.I | re.M)))
        # Explicit OS DLL list. Do not allow CRT api-ms-win-crt-* forwarding DLLs.
        allowed = {"KERNEL32.DLL", "ADVAPI32.DLL", "SHELL32.DLL", "OLE32.DLL", "OLEAUT32.DLL",
                   "VERSION.DLL", "WS2_32.DLL", "NTDLL.DLL", "USER32.DLL", "PSAPI.DLL",
                   "BCRYPT.DLL", "RPCRT4.DLL", "SHLWAPI.DLL", "UUID.DLL", "ICU.DLL"}
        result["needed"] = needed
        if not needed:
            result["violations"].append("dumpbin reported no DLL dependencies")
        unexpected = [n for n in needed if n.upper() not in allowed]
        if unexpected:
            result["violations"].append(f"Unexpected DLLs (including dynamic CRT): {unexpected}")
    result["pass"] = not result["violations"]
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", default="binary-audit.json")
    parser.add_argument("--observe", action="store_true")
    args = parser.parse_args()
    result = audit(args.binary)
    write_json(args.output, result)
    summary("Binary dependency audit", result)
    if not result["pass"] and not args.observe:
        raise SystemExit(1)
