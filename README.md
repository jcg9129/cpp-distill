<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# cpp-distill

`distill` turns a C++ competitive-programming solution and its personal headers
into one self-contained `.cpp`, resolves judge-known conditions, removes code it
can show unused, and checks the output with a real compiler. It keeps code when
it cannot establish safety, and falls back to a less transformed checked file.
It does not inline system headers, remove unused system includes, promise the
smallest output, support multiple source files or use MSVC as a judge compiler.
The intended behavior contract is the same stdout and exit status for the same
stdin; sample comparisons are checks, not a proof of runtime equivalence.

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
tar -xzf distill-0.1.0-linux-x86_64.tar.gz
```

On macOS use `shasum -a 256 distill-0.1.0-macos-arm64.tar.gz` and compare its
hex digest with the exact filename's entry in `SHA256SUMS`. Extract it, then
put `distill` on `PATH`. macOS may quarantine an unsigned downloaded executable;
after verifying and trusting the download, remove its quarantine attribute:

```sh
xattr -d com.apple.quarantine /path/to/distill
brew install gcc
```

On Windows compare `(Get-FileHash .\distill-0.1.0-windows-x86_64.zip -Algorithm SHA256).Hash`
with `SHA256SUMS`, extract the ZIP, and put `distill.exe` and your real MinGW
GCC on `PATH` (for MSYS2 UCRT64, typically `C:\msys64\ucrt64\bin`). PowerShell
uses `.\distill.exe` to run an executable in the current directory. Source,
output and report paths may contain spaces and Unicode characters.

## Usage

Distill options go **before the compiler name**. The remaining arguments are
the compile command you would normally use to build your solution:

```sh
distill -o submit.cpp g++ -std=gnu++23 -O2 -I ~/lib main.cpp
distill --judge codeforces -o submit.cpp g++ -I ~/lib main.cpp
distill --judge codeforces-gcc14-cpp23 -o submit.cpp g++ main.cpp
distill --judge list
distill main.cpp                       # same as: distill g++ main.cpp
distill --verify-run samples --report report.txt -o submit.cpp g++ -std=gnu++20 main.cpp
distill --version
```

Without an explicit compiler, `g++` is found on `PATH`. If it is Clang (including
Apple's stock `g++`), distill refuses it and asks for real GCC. On macOS select
your installed Homebrew compiler explicitly, for example `g++-15`. Explicitly
named Clang is accepted and noted, but the supported release smoke tests use
real GCC. The GCC installation must include the target's C/C++ standard headers
and libraries. [Input restrictions and command classification](docs/design.md#12-supported-input)
explain rejected features and options that limit transformations.

| Option | Meaning |
| --- | --- |
| `-o <file>` | Write checked output to this file; default stdout. |
| `--judge <name\|alias\|file>` | Select a bundled/user profile by full name, alias or TOML path; `--judge list` lists profiles. |
| `--keep <name>` | Add a pruning root by indexed name; repeatable. `// distill: keep` before a declaration also keeps it. |
| `--verify-run <path>` | Compare stdout and exit status on a sample file, or immediate `*.in` files in a directory, sorted without recursion; repeatable. |
| `--report <file>` | Write validation, configuration, conditional and pruning explanations. |
| `--no-prune` | Inline and resolve only. |
| `--strip-comments` | Strip comments during checked rendering; comments that must stay for directive/line safety can remain. |
| `--strict` | Exit 3 unless the written artifact is `pruned` with `target` verification. |
| `-v` | Show progress on stderr. |
| `--version` | Print the distill version, source commit and LLVM revision. |
| `--help` | Show help. |

Profiles have stable full names: `codeforces-gcc14-cpp23` and
`atcoder-gcc15-gnupp23`. The aliases `codeforces` and `atcoder` currently point
to those profiles; aliases may move to a judge's newest GCC option in later
releases. Use a full name to pin an option. Every selection prints
`judge: <selector> -> <resolved name>` on stderr. `--report` also records the
selector, resolved name, origin and SHA-256 profile content digest.

User profiles live in `$XDG_CONFIG_HOME/distill/judges` (when absolute), otherwise
`~/.config/distill/judges`, or `%APPDATA%\distill\judges` on Windows. A user name
or alias shadows the same bundled identifier with a warning on every run.
User names and aliases share a namespace; collisions between user profiles
are errors. User profile names follow the same shape as bundled ones
(`<judge>-gcc<major>-<cppNN|gnuppNN>[-<variant>]`, matching the file name);
every `.toml` in the user directory is validated on each lookup, so a user
profile from 0.1.0 with a free-form name must be renamed. A selector containing a path separator or ending in `.toml` loads
that file directly. `--judge list` needs no compile command and prints one line
per profile: name, alias (`-` if absent), origin (including user path),
reproduced/approximate, updated date and description. See
[data/judges/README.md](data/judges/README.md) for the schema and digest rule.

The result line is printed on stderr and begins the report, for example:

```text
distill: wrote pruned (local), runtime compared 2 samples
```

The selected artifact is the most transformed one that passed its checks:
`pruned`, otherwise `flat` (inlined and resolved), otherwise `raw` (inlined
only). Verification is `target` when the checking compiler's preprocessing
environment matches the reproduced judge environment, or `local` when it
satisfies the facts used by the transformation. Without a judge profile,
verification is local. Runtime status is `compared N samples`,
`skipped: can't run target binaries here`, or `not requested`.

| Exit code | Meaning |
| --- | --- |
| 0 | Checked source was written. |
| 1 | No source written: invalid/unsupported input, failed original build, or no checked artifact. |
| 3 | Source was written, but `--strict` did not get `pruned`/`target`. |

A pipeline failure may report `distill: nothing written, runtime ...`; early
input errors use `distill: <error>`. Diagnostics can precede the result line.
Output/report paths must differ from the input and each other.

## Judge profiles and conservative fallbacks

The bundled **Codeforces** profile is approximate: the judge's GCC label is
known, but its exact MSYS2 package revision is not. **AtCoder** has a reproduced
GCC build and snapshot from its published recipe. The D6c **CSES** profile
(`cses-gcc13-cpp23`, alias `cses`) records GCC 13.3.0 with C++23 and is
approximate: CSES publishes only the GCC version and flags, and its snapshot
comes from the exact Ubuntu 24.04 `g++-13` package that its other tool versions
point to. The D6c **QOJ** profile (`qoj-gcc15-cpp23`, alias `qoj`) records
GCC 15.2.0 with C++23 and is approximate: QOJ publishes no package revision;
CI uses the exact Ubuntu 26.04 `g++-15` package in a digest-pinned container.
QOJ also offers C++26. Its profile is deferred because the audited A6 ADL
name registry currently covers only C++11 through C++23; the dialect model
and standard header/macro inventories already cover C++26. The D6c **Kattis**
profile (`kattis-gcc15-gnupp23`, alias `kattis`) records GCC 15.2.0 with
GNU C++23 and is approximate. CI uses Kattis's digest-pinned `judge-cpp` image
only as snapshot provenance; its use in grading is UNVERIFIED. The language
page returned 403 on recheck, so its transcribed command remains UNVERIFIED.
The QOJ and Kattis snapshots and expected probe outputs must be captured by
branch CI before the PR; see
[Adding a profile](data/judges/README.md#research-and-ci).
A reproduced snapshot alone
does not make an arbitrary local GCC equivalent: target verification also needs
matching compiler facts, dialect and command. AtCoder's `-march=native` facts
vary across hosts, so that command does not establish target verification.
A compatible cross GCC can check target builds while runtime comparisons are
skipped when target binaries cannot run here. See
[judge profile provenance, caveats and custom profiles](data/judges/README.md).

Pruning is disabled when user conditions remain unknown, system headers are
outside the audited lists (including unaudited GNU extensions such as PBDS),
external libraries may call user code, required macro facts differ or are
unknown, or the dialect/options/AST features cannot be modeled safely. Clang
parse errors and worker limits also leave a checked fallback. Unmodeled command
options and user `__COUNTER__`, `push_macro`/`pop_macro` or `_Pragma` can disable
resolution too, leaving only `raw`. Some input features are rejected entirely.
Use `--report report.txt` to see exact disable reasons, retained declarations,
checks, assumptions and fallback failures. `flat` or `raw` is an intentional
result, not evidence that pruning succeeded.

The [design's trust assumptions A1–A6](docs/design.md#13-trust-assumptions-user-2026-11-15)
are:

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

`distill --judge-probe <selector> [--part N/M]` prints a diagnostic C++ program
for a judge's custom invocation/code test. Select the profile's exact compiler
option there and save its stdout. If the source or bounded output exceeds the
profile budgets, increase M and submit every part `1/M` through `M/M` separately.

```sh
distill --judge-probe codeforces --part 1/8 > probe.cpp
# Run all eight parts on the judge, saving their stdout, then:
distill --judge-check codeforces part*.txt --record observations
```

`--judge-check <selector> <output-file>...` validates the complete part manifest,
headers, counts and checksums, then reports matches, mismatches, missing, extra
and unsupported facts with checked/skipped counts. It exits 0 only if nothing
failed. Expected output ships beside the snapshot; until CI artifacts are added,
the command says `no expected output shipped for <profile>; run profiles.yml`.
`--record <dir>` saves dated, numbered evidence for a PR to
`data/judges/observations/`. `--judge list` shows the latest stored observation
for each profile digest, or `-`.

This is **diagnostic only**. A passing result is a **user-reported match** scoped
to that custom test and date. Pasted output is unauthenticated; equal expanded
strings do not establish equal macro definitions, and custom tests need not use
the grading configuration. Presence does not check function-like definitions or
builtin activity. Header/version diagnostics do not validate A1 inventories;
unlisted names remain untested and A4/A5 remain assumptions. Observations never
change distill's facts or narrow its native dependency model. A fact mismatch or
contradicted assumption on a shipped profile blocks the next release until
investigated; it does not automatically downgrade the profile.
