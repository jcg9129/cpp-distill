<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# A2 compiler/target identity macro audit (2026-11-18)

## Verdict and scope

**Sound only with library restrictions and fact provenance.** The unrestricted
proposal fails on real MinGW-w64 headers. Proceed with phase 2 using the finite
identity registry below, excluding the six `_M_*` aliases and `_WIN64` for
MinGW-w64. The new exemption preserves a fact only while it remains external, known (including
known-undefined), and every selected library is covered by its registry entry.
Unknown/absent library inventories use the intersection of all supported
libraries. No new facts are inferred from the list itself.

This is an explicit amendment to trust assumption A2(a), approved in direction
by the user on 2026-11-18, not a C++ standard guarantee. It does not prove all
versions of every vendor's headers. A1's reserved-name permission remains;
A2 supplies a narrower exception for these particular external facts. Do not
add these reserved names to the A1 non-reserved extension inventories.

## Attacks against real headers

Examined the installed GCC 15.2.0 Ubuntu headers and glibc 2.43 with the existing
textual standard-header/PBDS reachability scanner: 662 libstdc++ files and 212
glibc files; no proposed name occurs in a define or undef. This includes
inactive branches and transitive literal includes. The installed compiler banner
is `g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0`.

Downloaded public upstream header sources inside `build/audit/` and inspected
all `.h` and `.h.in` files (a broader scope than the standard-header closure):
1,858 MinGW-w64 files, 273 Apple Libc files, and 2,010 Apple XNU files. Upstream
branches were fetched on this audit date, not pinned releases or a complete
installed macOS SDK; CI's SDK inventories remain necessary.

- [MinGW-w64 `_mingw_mac.h`](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-headers/crt/_mingw_mac.h)
  synthesizes `_M_IX86`, `_M_AMD64`, `_M_X64`, `_M_ARM`, `_M_ARM64`, and
  `_M_ARM64EC` from GNU architecture macros. Ordinary CRT headers include
  [`_mingw.h`](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-headers/crt/_mingw.h.in),
  which includes this file: these are not merely unrelated Windows SDK names.
  Reject all six for MinGW-w64, even on another architecture where the textual
  branch is inactive. `_M_IA64` is another synthesized alias, outside the seed
  list; do not add it.
- [MinGW-w64 `_cygwin.h`](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-headers/crt/_cygwin.h)
  defines `_WIN64` under `__x86_64__`; `_mingw.h` reaches it under `__CYGWIN__`.
  Exclude `_WIN64` for MinGW-w64 as well. Tool-only WIDL headers also define
  `_WIN64`, but are not themselves evidence of standard-header reachability.
  No definition of `__MINGW32__`, `__MINGW64__`, or `_WIN32` was found in the
  downloaded headers. Library version macros such as `__MINGW32_MAJOR_VERSION`
  are different names and are deliberately excluded.
- [glibc `features.h`](https://github.com/bminor/glibc/blob/master/include/features.h)
  and its reachable installed headers consume compiler identity and define
  library/feature-policy names instead. `_WIN32_WINNT`, `_GNU_SOURCE`,
  `__GLIBC__`, and library feature-test macros are outside this registry.
- Apple's [Libc](https://github.com/apple-oss-distributions/Libc) and
  [XNU headers](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/cdefs.h)
  had no definitions of the proposed names. XNU provides fallback feature-query
  operators, demonstrating why all reserved/compiler-looking names cannot be
  exempted. Those operators are outside the finite identity list. This source
  audit is not a scan of proprietary or generated SDK headers.

Keep all 26 seed names, with the seven exclusions above. glibc, libstdc++, and
macOS libc have coverage for all 26; MinGW-w64 has coverage for the other 19.
Do not broaden to arbitrary target aliases, feature-query operators, CUDA
version macros, or library-defined feature/version macros. Libstdc++ coverage
alone does not cover the C library: all libraries selected by a profile must
permit a name. Unknown library names must refuse the exemption.

## Provenance, command operations, and verification

A list entry preserves an already known fact; it never supplies definedness or
a replacement. Reproduced snapshot absence (A4) is known-undefined. Approximate
profile absence is usable only under the existing approximate allowlist.
No-profile reserved facts remain unknown. In particular this amendment does
not upgrade the shipped approximate qoj profile to reproduced.

Profile `-D`/`-U` operations establish external initial facts in their ordered
command context and may survive a covered standard include. Local `-D`/`-U`
are still local baseline/raw settings; a selected profile replaces them for
flat/candidate checks. Source `#define` and `#undef`, including reserved names
like `_MSC_VER`, replace provenance with source provenance. They must keep the
current invalidation behavior at a standard include, even if their value equals
the original profile value. A branch join containing any source, header, pragma,
or earlier uncertain join provenance must not regain the exemption. A
non-standard include invalidates everything. The existing positive builtin
exemption must not accidentally preserve a source-written identity fact.

Satisfaction reruns compare external requirements and decisions against compiler
observations. Library-restricted facts need the checking configuration's library
policy too: the supported Linux, MinGW, and Darwin target families select the
same library inventories as the compatibility scanner; unsupported families use
the conservative intersection. This is library policy under A1/A2, not detection
of arbitrary replacement sysroots. Never copy the judge's library selection onto
an unrelated observed target merely to make satisfaction succeed.

A satisfaction rerun or an empty-source macro probe **does not independently
test A2**: both use the same header model. A matching target probe establishes
initial macro facts/target equivalence, not header behavior. The pipeline's
actual original/raw-to-flat preprocessing token/location comparison catches an
A2 violation that changes the transformed program in an available checking
configuration; compilation/runtime checks add coverage but are not proofs.
An unavailable judge remains subject to A2 as a trust assumption. CI must
separately inventory reachable textual identity definitions per library and
fail on any covered name, even an inactive or same-valued definition. Computed
include operands and unscanned versions remain scanner limitations, as for A1.

## qoj `__UINT64_C`: no clear bug

`data/judges/qoj-gcc15-cpp23-predefined.txt` contains the function-like
`__UINT64_C(c)` with a token-pasting replacement. Its header explicitly says
`reproduced: no`, `build: none`; the profile documents an approximate Ubuntu
snapshot rather than a verified judge package. `Profile::initialState` therefore
returns unknown: `data/judges/approximate-allowlist.txt` permits integer
MAX/MIN/WIDTH/TYPE facts, but not `_C` macro replacements. Presence in the
snapshot does not bypass that policy. The probe-expected file separately records
function-like macros as presence-only; that is not evidence of an exact judge
replacement either.

The pruning callback can encounter this name via expansion of `UINT64_C` from
`<cstdint>`. It correctly recognizes the compiler-predefined expansion and
requires a known, token-equivalent judge definition. The diagnostic “unknown
judge fact” is appropriate for this shipped profile. No code or allowlist fix:
adding `_C` to the approximate allowlist would need a separate proof about
suffix/type and token-pasting parity. A reproduced matching profile already
makes the stored definition usable. The reported external ATL run itself was
not rerun, so its exact invocation remains unverified.

## Separate scalar builtin change

The three generic integer overflow builtins perform scalar arithmetic and store
through an ordinary pointer argument. They have no implicit user lookup or
callbacks; traversing all argument expressions retains value producers and the
pointer producer. Their operand/result constraints and storage behavior are
specified in [GCC's overflow builtin documentation](https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html).
They fit `scalarBuiltin`'s implicit-declaration-only rule.
Add only these three names and an extraction regression whose third argument
calls a user function. Do not exempt explicitly declared functions or suppress
argument traversal.

## Implementation and verification

Implemented the sorted, embedded per-library identity registry through
`cmake/EmbedNames.cmake` and `names.cpp`; standard-include transitions preserve
only known external identity facts. Source writes and equal-value mixed joins
remain invalidated, and non-standard headers still invalidate all facts.
Observed configuration facts now select the supported target-family library
policy for satisfaction reruns. A1's extension inventories remain separate.

The scanner writes `*-identity.json` evidence for every scanned library,
including excluded definitions. Scan failure is fatal to the workflow, which
uploads evidence even on failure; merging independently validates matching
reports and rejects missing reports or covered definitions. Python regressions
cover inactive and spliced defines, comments/raw strings, scope exclusions,
invalid registries/reports, and scan/merge rejection.

Configured a Debug build with `DISTILL_LLVM_DIR` empty and
`DISTILL_PROFILE_ONLY=OFF`, built with `cmake --build build -j2`, and ran
`ctest --test-dir build -j2 --output-on-failure`: all six LLVM-free suites passed.
The unit suite reported 245 passes, zero failures, one platform-dependent skip.
It includes the reproduced-Linux guard, external/source provenance, command
operations, non-standard headers, approximate/no-profile behavior, and a
synthetic pruning-worker pipeline test that checks satisfaction, removal of an
unrelated function, and actual preprocessing rejection of a violating header.
The pipeline fixture suite passed the new `identity_macros_flat` case.

All 14 Python extension tests passed. A real local `scan.py --platform linux`
scan covered 212 glibc and 662 libstdc++ reachable headers with zero identity
definitions; `merge.py` accepted its reports. `git diff --check` passed.

No LLVM SDK was built or used. The real-worker `identity_macros` pruning fixture
and the three-overflow-builtin extraction regression were added but not run;
the LLVM-free build compiled the extraction test harness only. Cross-platform
MinGW/macOS workflow scans and the external 44-program ATL run were not run.
No commits were made.

## Target architecture extension: phase 1 audit (2026-11-18)

The reported x86 guard loses its absent `__i386__` fact at `<cstdint>` even
though the shipped approximate x86_64 snapshots already establish its absence.
The positive `__x86_64__` builtin exemption does not establish the other operands;
the resolver checks the complete dependency closure. Extend only the finite A2
registry, retaining its provenance and library intersection rules.

### Sources, pins, and method

Streamed `https://codeload.github.com/OWNER/REPO/tar.gz/COMMIT` archives at the
following pins. Counts are regular files inspected, not installed reachable
headers. Scanned all files in each archive, including templates, generators,
inactive branches and all architecture directories; for GCC, restricted to
`libstdc++-v3/` excluding `testsuite/` (including extensionless headers and
configuration inputs). No archive was built or installed.

| Source | Pin | Files | Candidate macro writes |
| --- | --- | ---: | --- |
| [glibc 2.42](https://github.com/bminor/glibc/tree/d2097651cc57834dbfcaa102ddfacae0d86cfb66) | `d2097651cc57834dbfcaa102ddfacae0d86cfb66` | 20553 | `__ARM_ARCH`, `__thumb__` |
| [GCC 15.2 libstdc++](https://github.com/gcc-mirror/gcc/tree/5115c7e447fc07457443df874bf57840e8316d5f/libstdc%2B%2B-v3) | `5115c7e447fc07457443df874bf57840e8316d5f` | 1819 | none |
| [MinGW-w64 master](https://github.com/mingw-w64/mingw-w64/tree/4fb6f0d5c6f4b945bddbc29402d9e3c11c776abd) | `4fb6f0d5c6f4b945bddbc29402d9e3c11c776abd` | 7036 | `__i386__` in WIDL tools only |
| [Apple Libc main](https://github.com/apple-oss-distributions/Libc/tree/71bbe350ab79eef58113991d817ccc6165061a64) | `71bbe350ab79eef58113991d817ccc6165061a64` | 1497 | none |
| [Apple XNU main](https://github.com/apple-oss-distributions/xnu/tree/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea) | `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea` | 5698 | `_LP64`, `__ILP32__` |

The source scan uses `generate.directive_text(text, mask_literals=True)` to
splice lines and mask comments/literals, followed by the identifier capture in
`^[ \t]*#[ \t]*(define|undef)[ \t]+([A-Za-z_][A-Za-z_0-9]*)`, intersected with
all 30 candidate names below. Both writes count, even inactive or same-valued.
The worktree's ignored `build/audit/` contains the script, resolved pins, JSON
write reports, matching source files and local CI-style scan reports.

### Per-library verdicts

“Keep” means only that the library permits preserving an already known external
fact. It does not admit new approximate-profile facts or assert that a name is
boolean-valued. The glibc and macOS exclusions below are deliberately broader
than proven standard-header reachability.

| Candidates | glibc | libstdc++ | MinGW-w64 | macOS libc |
| --- | --- | --- | --- | --- |
| `__i386__`, `__i386`, `__i486__`, `__i586__`, `__i686__` | keep | keep | keep | keep |
| `__x86_64__`, `__x86_64`, `__amd64__`, `__amd64` | keep | keep | keep | keep |
| `__aarch64__`, `__arm__` | keep | keep | keep | keep |
| `__thumb__`, `__ARM_ARCH` | exclude | keep | keep | keep |
| `__powerpc__`, `__powerpc64__`, `__ppc__`, `__PPC__` | keep | keep | keep | keep |
| `__riscv`, `__riscv_xlen` | keep | keep | keep | keep |
| `__s390__`, `__s390x__`, `__mips__`, `__loongarch__` | keep | keep | keep | keep |
| `__wasm__`, `__wasm32__`, `__wasm64__` | keep | keep | keep | keep |
| `__LP64__` | keep | keep | keep | keep |
| `__ILP32__`, `_LP64` | keep | keep | keep | exclude |
| `i386` | omit: existing A5/A2 non-reserved protection | same | same | same |

Evidence and boundaries:

- glibc's internal [ARM `sysdep.h`](https://github.com/bminor/glibc/blob/d2097651cc57834dbfcaa102ddfacae0d86cfb66/sysdeps/arm/sysdep.h)
  supplies numeric `__ARM_ARCH` fallbacks (2–7) and undefines `__thumb__` in an
  assembler branch. These are not demonstrated installed C++ header writes;
  conservatively exclude both for glibc rather than depend on build-only or
  language-mode reachability. Elsewhere the audit found only reads of these
  names. Numeric `__ARM_ARCH` and `__riscv_xlen` entries preserve a known
  replacement exactly; they never synthesize a value or turn absence into zero.
- Apple's [DTrace header](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/dtrace.h)
  defines `_LP64` from `__LP64__`. Keep `_LP64` off macOS coverage even without
  proving standard-header reachability. The separate spelling `__LP64__` has
  no writes. [MIG `device.defs`](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/device/device.defs)
  defines `__ILP32__` when `!__LP64__`. It is a generator input, not an installed
  C++ header; exclude macOS conservatively pending generated-SDK review.
- MinGW's [WIDL `minwindef.h`](https://github.com/mingw-w64/mingw-w64/blob/4fb6f0d5c6f4b945bddbc29402d9e3c11c776abd/mingw-w64-tools/widl/include/minwindef.h)
  synthesizes `__i386__` from `_X86_`. This is a separate tool include directory,
  not `mingw-w64-headers/` installed CRT/SDK input, and is outside the compiler's
  standard search paths. No candidate writes occur anywhere in
  `mingw-w64-headers/`. Keep `__i386__` for the CRT scope, consistently with the
  earlier distinction for WIDL-only `_WIN64` writes. Existing exclusions for
  the six `_M_*` aliases and `_WIN64` remain.
- `i386` is non-reserved (`isReserved` is false), absent from all four embedded
  A1 inventories, initially undefined under A5, and already preserved by
  `NameLists::protectedUserMacro`. Do not add it to the reserved identity list.
  `__SIZEOF_POINTER__` is not added either.
- `facts.cpp` already admits the shipped profiles' x86 names and data-model
  names through `approximate-allowlist.txt`. Several other candidates (for
  example `__ARM_ARCH`, `__riscv_xlen`, `__thumb__`) are **not** currently
  allowlisted. Their entries only benefit reproduced or explicit command
  facts; approximate unknowns remain unknown. No allowlist change is needed.

The existing `.github/workflows/extensions.yml` runs the same reachable-header
scanner for Linux GCC 14/15/16 and AlmaLinux, Windows MinGW, and both macOS
architectures. Its default registry now checks the additions automatically;
no platform-specific bypass is introduced. Extend `identity.definitions` to
capture `#undef` as well as `#define`, using the existing report field for both.
Scan and merge must reject either write for a covered name, including inactive
branches, and keep excluded writes in evidence. Source archives are not complete
installed SDKs: the local Linux scan and cross-platform CI remain complementary,
with the existing computed-include and unscanned-version limitations.

### Phase 2 changes and validation

Added 29 reserved candidates (55 total registry entries) with the exclusions
above; omitted non-reserved `i386`. `resolve.cpp` and `facts.cpp` need no code
changes: the existing external-fact transition and initial allowlist policy
handle the new registry entries. Updated design wording and CI's textual-write
scan, preserving the existing JSON report schema.

The real evaluator regression uses unmodified shipped approximate QOJ, CSES and
Kattis profiles, verifies initial and post-`<cstdint>` absence of `__i386__`,
and resolves the exact four-name x86 disjunction true. It also checks existing
non-reserved `i386` protection, source writes, non-standard includes and no-profile
unknowns. Name-policy tests check library exclusions and conservative inventory
intersection. The synthetic-worker pipeline regression uses shipped QOJ and
requires pruning, no unknown user condition, removal of an unrelated function,
actual preprocessing/build checks and matching sample execution. Like the
existing approximate-profile pipeline test, it skips incompatible host ABIs;
the x86 test skips on this aarch64 Linux host. No x86 pipeline execution is
claimed locally.

All 16 Python extension tests pass, including architecture writes under every
library policy, inactive/spliced `#undef`, excluded-write evidence and merge
rejection. The local `scan.py --platform linux` run covers 212 glibc and 662
libstdc++ reachable headers, with zero registered identity writes; `merge.py`
accepts the reports. Local packages are glibc `2.43-2ubuntu2.4` and GCC/libstdc++
`15.2.0-16ubuntu1`. This is an installed aarch64 scan, not an all-target proof.
Windows MinGW and macOS installed-header workflow scans were not run locally;
the shared scanner and registry enforce the same policy on all workflow jobs.

Configured Debug with `DISTILL_LLVM_DIR` empty and `DISTILL_PROFILE_ONLY=OFF`;
`cmake --build build -j2` succeeded. All five LLVM-free fixture suites passed
under `ctest --test-dir build -j2 --output-on-failure`. The concurrent unit run
hit its 120-second timeout; rerunning only that suite with
`ctest --test-dir build -R '^unit$' -j1 --output-on-failure` passed in 116 seconds:
256 passed, zero failed, two skips (Windows-only process test and x86-only
pipeline test). The targeted shipped-profile resolver and identity-name tests
also passed. `git diff --check` passed. No LLVM SDK was built or used, no
external 44-program run was performed, and no commits were made.
