<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# cpp-distill

`distill` turns a C++ competitive-programming solution and its personal headers
into one self-contained `.cpp`, resolves judge-known conditions, removes code it
can show unused, and checks the output with a real compiler. It keeps code when
it cannot establish safety, and falls back to a less transformed checked file.
The intended contract is the same stdout and exit status for the same stdin;
sample comparisons are checks, not a proof of runtime equivalence.

## Install

Download the archive for your platform and `SHA256SUMS` from
[GitHub Releases](https://github.com/jcg9129/cpp-distill/releases). Each archive
contains one executable, the documentation/licenses and `BUILDINFO.txt`; LLVM
libraries, profiles, name inventories and Clang resource headers are built in.
No LLVM installation or separate worker executable is needed.

| Archive platform | Requirement |
| --- | --- |
| `linux-x86_64`, `linux-arm64` | glibc 2.28 or newer; real GCC with C++ headers |
| `macos-arm64`, `macos-x86_64` | deployment minimum macOS 14.0; Homebrew GCC |
| `windows-x86_64` | 64-bit Windows; MSYS2 MinGW GCC (CI uses Windows 2025 and UCRT64) |

Linux binaries run in AlmaLinux 8 before release. macOS binaries are audited
for minos 14.0 and run on macOS 15; execution on macOS 14 is not yet tested
because that runner is unavailable. Windows compatibility is exercised on the
listed CI runner; no older Windows execution minimum has been established.

Verify the downloaded archive **before extraction**. For example, on Linux:

```sh
sha256sum --ignore-missing -c SHA256SUMS
# Require an OK result for your downloaded archive, then:
tar -xzf distill-0.1.1-linux-x86_64.tar.gz
```

On macOS use `shasum -a 256 distill-0.1.1-macos-arm64.tar.gz` and compare its
hex digest with the exact filename's entry in `SHA256SUMS`. Extract it, then
put `distill` on `PATH`. macOS may quarantine an unsigned downloaded executable;
after verifying and trusting the download, remove its quarantine attribute:

```sh
xattr -d com.apple.quarantine /path/to/distill
brew install gcc
```

On Windows compare `(Get-FileHash .\distill-0.1.1-windows-x86_64.zip -Algorithm SHA256).Hash`
with `SHA256SUMS`, extract the ZIP, and put `distill.exe` and your real MinGW
GCC on `PATH` (for MSYS2 UCRT64, typically `C:\msys64\ucrt64\bin`). PowerShell
uses `.\distill.exe` to run an executable in the current directory. Source,
output and report paths may contain spaces and Unicode characters.

## Quick start

Suppose `main.cpp` includes `"mylib/segtree.hpp"` from `~/lib/mylib/segtree.hpp`.
To submit to Codeforces:

```sh
distill --judge codeforces -o submit.cpp g++ -I ~/lib main.cpp
```

The profile supplies the judge dialect and macros. A successful pruned run includes these stderr lines:

```text
judge: codeforces -> codeforces-gcc14-cpp23
distill: wrote pruned (local), runtime not requested
```

Submit **`submit.cpp`** using Codeforces's **GNU G++23 14.2 (64 bit, msys2)**
option. Your personal headers are included in that file; system includes remain.

Codeforces is a Windows (MinGW) target. On Linux or macOS distill first tries the
profile's check compilers, prints lines such as
`check compiler unavailable: x86_64-w64-mingw32-g++` or `check compiler skipped:`
for unusable ones, and then checks with your own compiler (`local`). Installing a
MinGW cross compiler (`x86_64-w64-mingw32-g++`) lets it build against the judge's
target; samples are then compared only where those binaries can run.

## Everyday usage

Distill options go **before the compiler name**; compiler options go after it:

```sh
distill [distill options] <compiler> [compiler options] main.cpp
distill -o submit.cpp g++ -std=gnu++23 -O2 -I ~/lib main.cpp
distill main.cpp                       # same as: distill g++ main.cpp
distill --judge list
```

Without an explicit compiler, `g++` is found on `PATH`. If it is Clang (including
Apple's stock `g++`), distill refuses it and asks for real GCC. On macOS name
your Homebrew compiler explicitly, for example `g++-15`. Explicitly named Clang
is accepted and noted; release smoke tests use real GCC. The compiler installation
must include the target's C/C++ standard headers and libraries.

| Option | Meaning |
| --- | --- |
| `-o <file>` | Write checked output to this file; default stdout. |
| `--judge <selector>` | Profile name, alias or TOML path; `--judge list` lists profiles. |
| `--judge-probe <selector>` | Print diagnostic judge source; see checking below. |
| `--part N/M` | With `--judge-probe`, emit part N of M (default 1/1). |
| `--judge-check <selector> <output-file>...` | Compare saved judge diagnostics. |
| `--record <dir>` | With `--judge-check`, save dated observation evidence. |
| `--report <file>` | Write validation, configuration, conditional and pruning explanations. |
| `--no-prune` | Inline and resolve only. |
| `--keep <name>` | Add a pruning root by indexed name; repeatable. |
| `--strip-comments` | Strip comments during checked rendering; directive/line safety may require some to remain. |
| `--verify-run <path>` | Compare runs on a file or sorted directory `*.in`; repeatable. |
| `--strict` | Exit 3 unless the written artifact is `pruned` with `target` verification. |
| `-v` | Show progress on stderr. |
| `--help` | Show help. |
| `--version` | Print the distill version, source commit and LLVM revision. |

Judge diagnostics take no compile command or distillation options.
Output/report paths must differ from the input and each other.

| Exit code | Meaning |
| --- | --- |
| 0 | Checked source was written (or the requested diagnostic command succeeded). |
| 1 | Distillation wrote no source: invalid/unsupported input, failed original build, or no checked artifact; also diagnostic failure. |
| 3 | Source was written, but `--strict` did not get `pruned`/`target`. |

Pipeline failures may report `distill: nothing written, runtime ...`; early errors use
`distill: <error>`. Diagnostics can precede the result line.

## Understanding the result

The stderr result line also begins `--report`. Distill selects **`pruned`**,
otherwise **`flat`** (inlined and resolved), otherwise **`raw`** (inlined only).
The selected artifact passed its checks; `flat` and `raw` do not mean pruning succeeded.

Verification is **`target`** when the checking compiler's preprocessing environment
matches the reproduced judge environment, or **`local`** when it satisfies the facts
used by the transformation. Without a judge profile it is local. A reproduced snapshot
alone does not make arbitrary local GCC equivalent: compiler facts, dialect and command
must match. AtCoder's `-march=native` does not establish target verification across hosts.

Without `--judge`, judge compiler/platform facts are unknown by design; local
probes do not supply them. Pruning code with target-dependent conditionals needs
a profile with the required facts. Otherwise those conditionals stay unresolved
and disable pruning.

A profile's `inert` list guarantees only that its exact listed flags do not affect
preprocessing. It exempts those profile-origin occurrences from the unmodelled
option reason; the same unmodelled option typed locally still gets that reason.
An untranslated semantic option still disables pruning, including a profile-inert
flag: preprocessing inertness does not prove safe omission from the dependency parse.

Runtime status is `not requested`, `compared N samples`, or `skipped: can't run target binaries here`.
A compatible cross GCC can check a target build even when its binaries cannot run here.

```sh
distill --verify-run samples --report report.txt -o submit.cpp g++ -std=gnu++20 main.cpp
```

Put plain stdin bytes in `samples/01.in`, `samples/02.in`, etc. A directory uses
only immediate regular `*.in` files, sorted without recursion; it must not be
empty. A file path accepts any extension. Repeat `--verify-run` for more inputs.
There are **no `.out` expectations**: distill compares stdout byte for byte and
exit status between original and raw under the local command, original and flat
under the selected flat configuration, then flat and pruned. Each pair receives
the same stdin. Stderr is not compared. Samples do not prove correctness or
runtime equivalence; nondeterministic programs can fail these checks.

For a fallback, start with `--report report.txt`. Common reason text/prefixes:

| Report reason | What you can do |
| --- | --- |
| `pruning disabled: --no-prune` | Omit `--no-prune` to request pruning. |
| `unknown conditional in user code` | Inspect the reported `#if`; choose a judge profile with the needed facts, or accept `flat`. |
| `non-standard system header:` / `unaudited GNU extension header:` | PBDS and other unaudited system headers disable pruning; use audited headers or accept `flat`. |
| `external library option:` | External code may call user code; remove an unnecessary library option or accept `flat`. |
| `predefined macro NAME: unknown judge fact` / `predefined macro NAME differs: judge X, parse Y` | Choose a profile with the needed fact, or inspect the differing definitions and accept `flat`. Unknown parse facts are reported separately. |
| `external or unknown token-paste operand:` | Inspect the macro; avoid compiler-dependent pasting or accept `flat`. |
| `unsupported ADL dialect:` / `untranslated semantic option:` / `unsupported user pragma` | Use a supported C++11–C++23 dialect/options/constructs where possible, or accept `flat`. |
| `Clang parse error` / `pruning worker deadline exceeded` | GCC may accept code the analysis cannot handle; simplify it or accept the checked fallback. |
| `resolution disabled: __COUNTER__` (also `_Pragma`, `push_macro`, `pop_macro`) / `unmodelled option:` | Remove the feature/option if unnecessary; otherwise expect `raw`. |
| `check compiler unavailable:` / `configuration does not satisfy resolution` | Install a compatible compiler named by the profile; if no configuration satisfies the facts, expect `raw`. |
| `pruned rejected:` / `flat rejected:` | Inspect the failed build, invariant or sample check; distill keeps the last checked artifact. |

To retain a declaration, add its indexed name with `--keep` or put this comment before it:

```cpp
// distill: keep
void debug_state() { /* optional debugging hook */ }
```

Use `distill --keep debug_state --report report.txt -o submit.cpp g++ -std=gnu++20 main.cpp`.
An unknown `--keep` name is an error; it requires a checked flat configuration.
Annotations are captured before comment stripping.

Read the report's first line for the artifact/verification/runtime result, then
`profile`, `assumptions`, option classifications and per-artifact checks.
For a header-free `int main(){}`, `distill --no-prune --report report.txt g++ -std=gnu++20 main.cpp`
starts the report like this:

```text
distill: wrote flat (local), runtime not requested
profile: none
assumptions: none
```

Later, `baseline: passed` confirms the original build, and
`pruning disabled: --no-prune` explains why pruning was skipped.
Checks include the actual configuration argv, compiler and option changes.
Conditional entries explain resolved/dead/unknown branches and facts. Pruning
analysis lists `family` retention reasons, dependency causes and byte counts;
its heading says `pruning analysis (does not certify an artifact)` because a later
check can reject that candidate. See [report details](docs/design.md#49-report).

## Judges

| Full name | Alias | Judge | Compiler | Standard | Status |
| --- | --- | --- | --- | --- | --- |
| `atcoder-gcc15-gnupp23` | `atcoder` | AtCoder | GCC 15.2.0 | `gnu++23` | Reproduced |
| `codeforces-gcc14-cpp23` | `codeforces` | Codeforces | GCC 14.2.0 | `c++23` | Approximate |
| `cses-gcc13-cpp23` | `cses` | CSES | GCC 13.3.0 | `c++23` | Approximate |
| `kattis-gcc15-gnupp23` | `kattis` | Kattis | GCC 15.2.0 | `gnu++23` | Approximate |
| `library-checker-gcc15-cpp23` | `library-checker` | Library Checker | GCC 15.2.0 | `c++23` | Approximate |
| `qoj-gcc15-cpp23` | `qoj` | QOJ | GCC 15.2.0 | `c++23` | Approximate |
| `timus-gcc13-cpp23` | `timus` | Timus | GCC 13.2.0 | `c++23` | Approximate |
| `yukicoder-gcc16-cpp23` | `yukicoder` | yukicoder | GCC 16.2.0 | `c++23` | Approximate |
| `yukicoder-gcc16-gnupp23` | — | yukicoder | GCC 16.2.0 | `gnu++23` | Approximate |

Use `--judge codeforces-gcc14-cpp23` to pin a compiler option. Full names never
change meaning; aliases track the newest supported GCC option and may move in
later releases (moves belong in release notes). Each selection prints
`judge: <selector> -> <resolved name>` on stderr. Reports also record selector,
resolved name, origin and SHA-256 profile content digest.

User profiles live in `$XDG_CONFIG_HOME/distill/judges` (when absolute), otherwise
`~/.config/distill/judges`, or `%APPDATA%\distill\judges` on Windows. Names use
`<judge>-gcc<major>-<cppNN|gnuppNN>[-<variant>]`, matching the TOML filename stem;
rename free-form names from 0.1.0. User names/aliases shadow identical bundled identifiers
with a warning on every run. User collisions are errors; every user `.toml` is validated
on lookup. A selector containing a path separator or ending in `.toml` loads directly.
`--judge list` needs no compile command: it shows name, alias (`-` if absent), origin
(with user path), reproduced/approximate, updated date, description and latest stored
observation for that profile digest (`-` if absent).

Profiles reviewed more than **180 days** ago print a one-line stderr note
suggesting `--judge-check`; facts, output and exit status stay the same.
Weekly evidence/availability checks and monthly snapshot reproduction track
[maintenance](data/judges/README.md#maintenance) separately.

Reproduced profiles use the published judge package/recipe; approximate profiles lack
that build identity or complete compiler coverage and use conservative facts.
Only AtCoder is reproduced here; native CPU facts remain conservative.
See [provenance, caveats, schema and digest rules](data/judges/README.md).

## Checking a judge yourself

`--judge-probe` prints a diagnostic C++ program. Select the profile's exact compiler
option on the judge, use custom invocation/code test where available, and save stdout.
Use the same distill version to generate and check. If source or bounded output exceeds
profile budgets, increase M and submit **every** part `1/M` through `M/M` separately:

```sh
distill --judge-probe codeforces --part 1/8 > probe.cpp
# Generate/run all eight parts and save their stdout as part1.txt ... part8.txt.
distill --judge-check codeforces part*.txt --record observations
```

All nine bundled profiles ship `<name>-probe-expected.txt`. `--judge-check` validates
complete part manifests, headers, counts and checksums, then reports matches, mismatches,
missing, extra and unsupported facts with checked/skipped counts. It exits 0 only when
nothing failed. Missing expectations (e.g. in a user profile) explicitly fail.
`--record <dir>` saves dated, numbered evidence for `data/judges/observations/` review.

Some judges expose only compiler output. **Timus:** submit a deliberately failing
program to see manual `#pragma message` diagnostics; see [procedure and limits](data/judges/README.md#research-and-ci).
The generated runtime probe cannot use that channel unchanged, and manual
pragma messages are not valid `--judge-check` output.

This is **diagnostic only**: a pass is a **user-reported match** for that custom
test and date. Pasted output is unauthenticated; equal expanded strings do not
establish equal macro definitions, and custom tests need not match grading.
Presence does not check function-like definitions or builtin activity.
Header/version diagnostics do not validate A1 inventories; unlisted names remain
untested and A4/A5 remain assumptions. Observations never change profile facts
or narrow the native dependency model.

## Limitations and trust assumptions

Distill does not inline system headers, remove unused system includes, promise
the smallest output, support multiple source files or use MSVC as a judge compiler.
Some features are rejected, including computed user includes, user `#line`/`#include_next`
and location-sensitive builtins. See [input restrictions](docs/design.md#12-supported-input)
and [command classification](docs/design.md#41-command-model) for the exact scope.
Pruning can keep more code or be disabled for unsupported options/AST features.

The [trust assumptions A1–A6](docs/design.md#13-trust-assumptions-user-2026-11-15) are:

- **A1:** Standard headers introduce only specified, reserved or inventoried extension names.
- **A2:** Those headers do not alter protected user/compiler macros, and macro expansions refer to user names only through identifiers present in their arguments.
- **A3:** The judge does not predefine macros defined by the user's code.
- **A4:** Profile facts are accurate at their recorded date; a reproduced build matches the published judge package/recipe without extra patches or startup settings.
- **A5:** The judge adds no non-reserved macros beyond the profile's operations.
- **A6:** Standard-library and language-generated lookup of user functions uses the audited ADL/customization names or operators; separate rules protect callbacks and specializations.

## Build, license and credits

See [the core/build guide](src/core/README.md) for CMake, LLVM-free tests,
pinned LLVM SDK builds and the release workflow. Release binaries use the
same toolchains as the [compatibility matrix](.github/workflows/matrix.yml).

cpp-distill is **GPL-3.0-or-later**, without warranty; see [LICENSE](LICENSE).
GPL corresponding source for release binaries is supplied by the release tag's
source archive, including build scripts, plus the unmodified llvm-project source
archive attached to the `llvm-23.1.5-1` SDK release. Each release links both.
[LLVM-LICENSE.txt](LLVM-LICENSE.txt) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) cover LLVM/Clang, embedded
headers, bundled support code and platform runtimes.

This is a from-scratch successor to
[caide-cpp-inliner by slycelote](https://github.com/slycelote/caide-cpp-inliner).
Its design lessons and the user's GPL caide fork's test corpus informed this
implementation. Thanks to the LLVM/Clang and GCC contributors.
