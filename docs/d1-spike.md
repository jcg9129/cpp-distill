<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# D1: distribution spike

D1 asks whether GitHub-hosted runners can build and distribute static LLVM/Clang SDKs and standalone `distill` executables within their memory, disk and time budgets. It does not implement inlining or pruning. No measurements below have been invented: the reviewer fills the results after running CI. No LLVM or Docker build is needed locally.

## Run the workflows

These commands assume the workflows are on the selected branch and `gh` targets this repository. Substitute an available official LLVM release if necessary. All downloads use the exact named release assets; missing assets fail explicitly.

```bash
# Build all five SDKs without publishing. At most 350 minutes per build.
gh workflow run llvm-sdk.yml -f llvm_version=23.1.5 -f sdk_rev=1 -f platforms=all -F publish=false
# Or select a subset (a comma-separated list without spaces).
gh workflow run llvm-sdk.yml -f llvm_version=23.1.5 -f sdk_rev=1 -f platforms=linux-x86_64,macos-arm64 -F publish=false
# Publish llvm-23.1.5-1 on this workflow's commit, with corresponding source.
gh workflow run llvm-sdk.yml -f llvm_version=23.1.5 -f sdk_rev=1 -f platforms=all -F publish=true
# Observe four official archives; there is no macOS x86_64 official archive.
gh workflow run llvm-audit.yml -f llvm_version=23.1.5
# Consume the published llvm-23.1.5-1 SDKs (also the pull_request default).
gh workflow run spike.yml -f sdk_source=release -f llvm_version=23.1.5 -f sdk_rev=1
# Consume an unpublished SDK run in this repository.
gh workflow run spike.yml -f sdk_source=run -f sdk_run_id=123456789 -f llvm_version=23.1.5 -f sdk_rev=1
# Try the official archives through the same build and smoke gates.
gh workflow run spike.yml -f sdk_source=official -f llvm_version=23.1.5

gh run list
gh run view RUN_ID --log
gh run download RUN_ID -D d1-artifacts
```

`spike.yml` also accepts `platforms`. Official mode omits macOS x86_64. SDK builds never cancel an in-progress build; newer PR/dispatch spike runs cancel earlier runs for the same PR/ref. Publishing alone has `contents: write`; the spike has `actions: read` to fetch artifacts across runs. No personal token is used. First publish a matching SDK before expecting the default PR workflow to succeed.

## SDK settings and artifacts

The identity is **LLVM version + sdk_rev + platform**. Bump `sdk_rev` whenever `build-sdk.py` settings change, including dependency/runtime choices. A release tag already targeting a different commit is rejected. Repeating a subset on the same commit preserves other platforms and their checksums.

The source is LLVM's unmodified `llvm-project-<version>.src.tar.xz`. Published GitHub asset SHA256 digests or checksum assets are verified when available; otherwise its computed SHA256 is recorded explicitly as unverified against an upstream checksum. No signature verification is claimed. The source archive is uploaded once per run and included in published releases, together with per-platform `measurements-<platform>.json` and `SHA256SUMS`. Asset contents (including their upstream licenses) remain unmodified. Files larger than 2 GiB are split into `.part000`, `.part001`, etc.; concatenate in lexical order, verify each part with `SHA256SUMS`, then extract. The spike downloader does this automatically. SDK workflow artifacts retain the unsplit archive; the 2 GiB limit concerns release assets.

Linux uses manylinux_2_28's newest installed GCC toolset, not the Ubuntu host compiler. SDK container jobs explicitly mount the disposable runner's Android, .NET and GHC directories at `/opt/unused-*`; if free space is below 20 GiB, `linux-cleanup.sh` removes their contents before building. The spike and audit workflows apply the same cleanup to the host before pulling Docker images. It never removes the runner toolcache or workspace. This helper is CI-only. Initial SDK measurements are taken after that cleanup; `df` before and after cleanup is in the job log. Linux smoke jobs use Docker only for compilation/auditing, then execute the moved binary with Ubuntu's own GCC and AlmaLinux 8's GCC.

Builds use Release, no debug info, no assertions, no LTO, native LLVM backend only, static libraries, disabled optional compression/XML/editing/performance/network dependencies, all runner cores for compilation, one link job. MSVC uses `/MT`; macOS uses explicit architecture and deployment target 14.0. CMake and Ninja versions are fixed in CI. Container tags and OS runner images follow their providers and are recorded by GitHub; they are not immutable image digests.

`LLVM_BUILD_TOOLS=OFF` and `CLANG_BUILD_TOOLS=OFF` are combined with LLVM's `distribution`/`install-distribution` targets. The `llvm-libraries` and `clang-libraries` umbrellas install all library exports, plus LLVM/Clang development headers, generated headers, resource headers and both CMake packages. Configured executable targets are excluded from the export distribution, avoiding dangling references to uninstalled tools. Required host tablegen programs still build. The unused libclang, libLTO and libRemarks C API wrappers (which otherwise build shared libraries), the LLVM shared-library tool, static analyzer, and ARC migration are disabled. All remaining static library umbrellas are intentionally included to keep transitive exports complete; only the native backend is configured. The consumer checks `find_package(Clang CONFIG)` and static Clang target types. First CI runs must validate these upstream distribution components for the selected release.

Each SDK artifact contains `llvm-sdk-<version>-<sdk_rev>-<platform>.tar.xz` (one `sdk/` root) and `measurements.json`; full build logs are separate artifacts. JSON and job summaries record configure/build/install/archive seconds, source/build/install apparent bytes, compressed archive bytes, cores, runner memory, free disk, source SHA256/verification and complete CMake settings. Linux GNU time and macOS BSD time report maximum RSS for a build process, **not aggregate concurrent compiler RSS**. Windows RSS is `null` with an explicit n/a note. Disk numbers are apparent file sizes, not allocated filesystem blocks. Failed build measurements are retained when the process can finalize; runner timeout/OOM termination can prevent finalization.

## Official archive audit

`llvm-audit.yml` runs on each matching platform, using manylinux 2.28 for Linux. It inventories required Clang/LLVM static libraries, CMake exports, generated headers and resource headers. It then configures and links the actual stub using the same `build-stub.py` used by spike CI. Missing libraries/exports and configure/link failures are evidence of an unusable archive and are recorded without failing this observational audit. Download/infrastructure errors still fail the job.

`audit.json`, configure/link logs, acquisition metadata, and the job summary contain the evidence. Linux installs development packages for zlib, zstd, libxml2, ncurses and libedit so official CMake exports can resolve them; export lines show declared optional dependencies, link logs show unresolved dependencies, and a successfully linked binary is examined with `readelf -d` and `objdump -T`. **The reported GLIBC maximum comes from the linked stub inside manylinux, never from the clang executable shipped upstream.** No value is reported when the stub cannot link. macOS samples one extracted `.o` per required archive and reports its `minos` (not a guarantee about every member). Windows scans all member directives of each required `.lib` and records DEFAULTLIB names, exposing LIBCMT vs MSVCRT. A successful link is also checked for runtime DLL dependencies. The `usable` field requires the dependency gate and embedded self-check as well as a successful consumer link.

## Stub behavior and validation

Given a prepared SDK, a normal consumer build is:

```bash
cmake -S spike -B build -G Ninja -DLLVM_SDK=/absolute/path/to/sdk -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/distill-stub --list-resources
./build/distill-stub --self-check
./build/distill-stub /usr/bin/g++ spike/tests/smoke.cpp
./build/distill-stub --foreign-check spike/tests/smoke-portable.cpp
```

On Windows use an MSVC developer PowerShell, `/MT`, and the `.exe` suffix. These commands are for CI or a provisioned developer machine; do not try them on the low-memory D1 editing environment.

The C++20 host generator embeds every resource file in numeric byte arrays, one translation unit per file to limit compiler memory. Each array has a NUL sentinel required by the Clang lexer, excluded from its logical file size. It requires no Python at consumer build time. Python 3.12+ is used only for CI orchestration, downloads, measurement and reporting (safe tar extraction uses `filter="data"`). A SHA-pinned setup-python action selects Python 3.12 on macOS/Windows; manylinux supplies `cp312`.

Compiler probing uses LLVM's process API with a 30-second timeout and no shell. Output and diagnostics go to temporary files, never `/dev/null`. Include paths keep their order and their quote/system/framework distinction. Backslashes and `..` components are normalized for classification. Only paths ending in `gcc/<triple>/<version>/include` or `include-fixed` (also `gcc-cross`) are filtered, where the version begins with a digit. This avoids mistaking MinGW target C headers reached through `../../..` or Homebrew paths containing the GCC package name for builtin headers. These are GCC's private builtin headers, which contain GCC-specific builtins. Its `include/c++` directories and target system headers are retained. Embedded Clang headers are explicitly first in the system search order so `include_next` chains enter compatible builtin headers. Probe output must contain both the include-list markers and a terminator; malformed probes fail. Extra user arguments follow defaults and can override `-std` or `-target`.

An in-memory VFS overlays the real filesystem **before** ToolInvocation creates the compiler invocation. It uses a fixed virtual resource directory (drive-qualified on Windows), without extracting resource files. The freestanding `--self-check` includes `stddef.h`, `stdarg.h` and `stdint.h` with no host compiler or system headers. `--foreign-check` is an explicit no-probe mode targeting `x86_64-w64-mingw32`; its tiny freestanding program needs only resource headers. This tests parsing/TargetInfo, not foreign code generation or a foreign standard library. Native-backend-only SDKs are expected to parse it; its exit/output is recorded and never gates the job.

After building, a fresh step moves the binary into an empty directory and deletes the SDK and build tree. Dependency gates enforce Linux's explicit NEEDED allow-list and GLIBC ≤ 2.28, macOS's three allowed system dylibs and `minos` 14.0, and Windows's explicit OS DLL allow-list (no dynamic CRT, including CRT forwarding DLLs). Smoke commands have 120-second timeouts; probes have their own 30-second timeout. Native tests include GCC's `bits/stdc++.h`, architecture intrinsics, C builtin headers, concepts, spans and ranges. The AlmaLinux 8 test overrides the language mode to GNU C++17 for GCC 8's library. Windows additionally tests a path containing spaces and `é`; the stub converts wide argv to UTF-8 before invoking LLVM. Windows GCC is installed in the runner's existing `C:\msys64` UCRT64 environment. macOS detects the newest preinstalled Homebrew `g++-N`, installing GCC only if none exists.

`spike-<platform>` artifacts contain the executable, dependency audit, isolation evidence, smoke JSON and full logs. Artifact downloads may lose Unix executable permission; use `chmod +x` before manual runs. The final `spike-combined-summary` artifact merges platform JSON, and all measurements are visible in job summaries. Required smoke/dependency failures gate the workflow. The foreign-triple result is observational. The AlmaLinux JSON and log record the old-glibc run. Smoke observations still run after dependency gate failures.

First runs should watch runner disk/RSS/time budgets, upstream component names and Clang API changes, static export completeness, official SDK CRT/dependency mismatches, Homebrew GCC/Apple SDK header compatibility and GCC/Clang builtin differences. Actual binaries and source archives are deliberately not built/downloaded in the local editing environment. Lightweight script/workflow checks cannot establish runtime portability.

## Lightweight local checks

```bash
python3 ci/llvm/test-ci.py
python3 -m py_compile ci/llvm/*.py
for script in ci/llvm/*.sh; do bash -n "$script"; done
shellcheck ci/llvm/*.sh
actionlint
```

The fixture tests cover platform selection, dependency/deployment parsing and tar traversal rejection without invoking a compiler or Docker. They create temporary files only under this checkout's `.checks/` directory.

## Results

| Platform | Build time | Peak RSS | Disk (source/build/install) | Archive size | Official archive usable? | glibc / minos / CRT | Smoke result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Linux x86_64 | | | | | | | |
| Linux arm64 | | | | | | | |
| macOS arm64 | | | | | | | |
| macOS x86_64 | | | | | N/A: no official archive | | |
| Windows x86_64 | | | | | | | |
