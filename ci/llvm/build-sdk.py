# SPDX-License-Identifier: GPL-3.0-or-later
"""Build only the SDK distribution. Settings changes require a new sdk_rev."""
import argparse
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import time
from common import PLATFORMS, capture, download_asset, extract, release, run, summary, tree_bytes, write_json

parser = argparse.ArgumentParser()
parser.add_argument("--version", required=True)
parser.add_argument("--rev", required=True, type=int)
parser.add_argument("--platform", required=True, choices=[p["platform"] for p in PLATFORMS])
args = parser.parse_args()
config = next(p for p in PLATFORMS if p["platform"] == args.platform)
work = Path("sdk-work").resolve()
work.mkdir(exist_ok=True)
out = Path("out").resolve()
out.mkdir(exist_ok=True)
source = work / "source"
build = work / "build"
prefix = work / "sdk"
measurements = dict(platform=args.platform, llvm_version=args.version, sdk_rev=args.rev,
                    cores=os.cpu_count(), disk_free_before_bytes=shutil.disk_usage(work).free,
                    times_seconds={}, peak_rss_bytes=None, status="in progress")
if platform.system() == "Linux":
    measurements["runner_memory"] = Path("/proc/meminfo").read_text().splitlines()[0]
    measurements["runner_memory_bytes"] = int(measurements["runner_memory"].split()[1]) * 1024
elif platform.system() == "Darwin":
    measurements["runner_memory_bytes"] = int(capture(["sysctl", "-n", "hw.memsize"]).strip())
else:
    measurements["runner_memory_bytes"] = int(capture(["pwsh", "-NoProfile", "-Command", "(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory"]).strip())
    measurements["peak_rss_note"] = "n/a: Windows child-process-tree peak RSS is not measured"


def timed(name, command):
    start = time.monotonic()
    try:
        run(command)
    finally:
        measurements["times_seconds"][name] = round(time.monotonic() - start, 3)


try:
    archive, verification = download_asset(release(args.version), f"llvm-project-{args.version}.src.tar.xz", work / "downloads")
    measurements["source_archive"] = verification
    measurements["source_archive_bytes"] = archive.stat().st_size
    extract(archive, source)
    llvm = source / f"llvm-project-{args.version}.src" / "llvm"
    # Distribution umbrellas include all static libraries and only their exports.
    # LLVM_BUILD_TOOLS=OFF still permits required host tablegen tools. Clang tools
    # remain configured (CLANG_BUILD_TOOLS=OFF) but are excluded from distribution,
    # so ClangConfig cannot reference uninstalled clang/clang-format executables.
    components = "llvm-libraries;clang-libraries;llvm-headers;clang-headers;clang-resource-headers;cmake-exports;clang-cmake-exports"
    options = dict(
        CMAKE_BUILD_TYPE="Release", CMAKE_INSTALL_PREFIX=prefix, CMAKE_INSTALL_LIBDIR="lib",
        CMAKE_INSTALL_PACKAGEDIR="lib/cmake", LLVM_ENABLE_PROJECTS="clang",
        LLVM_TARGETS_TO_BUILD=config["target"], LLVM_ENABLE_ASSERTIONS="OFF",
        LLVM_ENABLE_LTO="OFF", LLVM_ENABLE_PDB="OFF", LLVM_ENABLE_ZLIB="OFF", LLVM_ENABLE_ZSTD="OFF",
        LLVM_ENABLE_LIBXML2="OFF", LLVM_ENABLE_TERMINFO="OFF", LLVM_ENABLE_LIBEDIT="OFF",
        LLVM_ENABLE_LIBPFM="OFF", LLVM_ENABLE_CURL="OFF", LLVM_ENABLE_Z3_SOLVER="OFF",
        LLVM_ENABLE_HTTPLIB="OFF", CLANG_ENABLE_LIBXML2="OFF",
        LLVM_INCLUDE_TESTS="OFF", LLVM_INCLUDE_EXAMPLES="OFF", LLVM_INCLUDE_BENCHMARKS="OFF",
        LLVM_INCLUDE_DOCS="OFF", CLANG_INCLUDE_TESTS="OFF", CLANG_INCLUDE_DOCS="OFF",
        LLVM_BUILD_TOOLS="OFF", CLANG_BUILD_TOOLS="OFF", LLVM_BUILD_UTILS="OFF",
        LLVM_TOOL_LTO_BUILD="OFF", LLVM_TOOL_REMARKS_SHLIB_BUILD="OFF",
        LLVM_TOOL_LLVM_SHLIB_BUILD="OFF", LLVM_BUILD_LLVM_C_DYLIB="OFF",
        BUILD_SHARED_LIBS="OFF", LLVM_BUILD_LLVM_DYLIB="OFF", LLVM_LINK_LLVM_DYLIB="OFF",
        CLANG_LINK_CLANG_DYLIB="OFF", CLANG_BUILD_CLANG_DYLIB="OFF",
        # The product uses C++ APIs. Exclude libclang's always-shared C API and
        # unused analyzer/ARC migration libraries rather than packaging DLLs.
        CLANG_TOOL_LIBCLANG_BUILD="OFF", CLANG_ENABLE_STATIC_ANALYZER="OFF",
        CLANG_ENABLE_ARCMT="OFF", LLVM_PARALLEL_LINK_JOBS="1",
        LLVM_PARALLEL_COMPILE_JOBS=os.cpu_count(), LLVM_DISTRIBUTION_COMPONENTS=components)
    if platform.system() == "Windows":
        options.update(CMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded",
                       CMAKE_C_FLAGS_RELEASE="/O2 /DNDEBUG", CMAKE_CXX_FLAGS_RELEASE="/O2 /DNDEBUG")
    else:
        options.update(CMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -g0", CMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -g0")
    if platform.system() == "Darwin":
        options.update(CMAKE_OSX_ARCHITECTURES=config["arch"], CMAKE_OSX_DEPLOYMENT_TARGET="14.0")
    measurements["cmake_settings"] = {key: str(value) for key, value in options.items()}
    timed("configure", ["cmake", "-S", llvm, "-B", build, "-G", "Ninja"] + [f"-D{k}={v}" for k, v in options.items()])
    command = ["cmake", "--build", build, "--target", "distribution", "--parallel", str(os.cpu_count())]
    rss_log = work / "build-time.txt"
    if platform.system() == "Linux":
        command = ["/usr/bin/time", "-v", "-o", rss_log] + command
    elif platform.system() == "Darwin":
        # BSD time writes to stderr; keep build output and timing together.
        command = ["/usr/bin/time", "-l"] + command
    start = time.monotonic()
    try:
        with (work / "build.log").open("w", encoding="utf-8") as log:
            # Tee without a shell, retaining the complete build log as an artifact.
            with subprocess.Popen(list(map(str, command)), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace") as process:
                for line in process.stdout:
                    print(line, end="", flush=True)
                    log.write(line)
                if process.wait():
                    raise RuntimeError(f"SDK build failed: {process.returncode}")
    finally:
        measurements["times_seconds"]["build"] = round(time.monotonic() - start, 3)
        log = rss_log.read_text() if rss_log.exists() else (work / "build.log").read_text()
        match = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", log)
        if match:
            measurements["peak_rss_bytes"] = int(match[1]) * 1024
        match = re.search(r"(\d+)\s+maximum resident set size", log)
        if match:
            measurements["peak_rss_bytes"] = int(match[1])
        measurements["peak_rss_scope"] = "time reports maximum RSS of a build process, not sum of simultaneous compiler processes"
    timed("install", ["cmake", "--build", build, "--target", "install-distribution", "--parallel", "1"])
    for required in ["lib/cmake/clang/ClangConfig.cmake", "lib/cmake/llvm/LLVMConfig.cmake", "include/clang/Basic/Version.inc", "include/llvm/Config/llvm-config.h", f"lib/clang/{args.version.split('.')[0]}/include/stddef.h"]:
        if not (prefix / required).is_file():
            raise RuntimeError(f"SDK missing {required}")
    for library in ["clangTooling", "clangFrontend", "clangDriver", "clangSema", "clangAST",
                    "clangLex", "clangBasic", "clangSerialization", "clangParse", "clangAnalysis",
                    "clangEdit", "clangASTMatchers", "LLVMSupport", "LLVMOption",
                    "LLVMFrontendOpenMP", "LLVMTargetParser", f"LLVM{config['target']}Info"]:
        name = f"{library}.lib" if platform.system() == "Windows" else f"lib{library}.a"
        if not (prefix / "lib" / name).is_file():
            raise RuntimeError(f"SDK missing static library {name}")
    destination = out / f"llvm-sdk-{args.version}-{args.rev}-{args.platform}.tar.xz"
    # cmake's libarchive uses bounded memory; Python lzma's default is also small.
    timed("archive", ["cmake", "-E", "chdir", work, "cmake", "-E", "tar", "cJf", destination, "sdk"])
    measurements["archive_bytes"] = destination.stat().st_size
    measurements["status"] = "success"
except Exception as error:
    measurements["status"] = "failed"
    measurements["failure"] = str(error)
    raise
finally:
    measurements["disk_bytes"] = {"source": tree_bytes(source), "build": tree_bytes(build), "install": tree_bytes(prefix)}
    measurements["disk_free_after_bytes"] = shutil.disk_usage(work).free
    write_json(out / "measurements.json", measurements)
    summary(f"LLVM SDK: {args.platform}", measurements)
