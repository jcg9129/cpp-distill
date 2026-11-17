<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Core library (D2a–D5a)

`distill_core` is a C++20 static library with no LLVM or third-party dependency.
`command/` expands GCC response files and classifies a compile invocation,
retaining argument spelling and order. Its queries expose include paths,
local macros, dialect and reasons that transformations must be disabled.
`buildArgv` replaces source/output paths, drops dependency outputs, optionally
keeps local macros, and appends defines (bare names or `NAME=value`). Its
`generatedFile` parameter defaults to true, dropping `-I`/`-iquote`; pass false
for baseline builds. Paths in the command retain their original spelling and
are interpreted relative to the command's absolute `cwd`.

Response files use whitespace, quotes and backslash escapes, with at most
16 levels of nesting. All relative response filenames use the command cwd,
including nested files. Missing files, cycles, unmatched quotes and dangling
escapes are errors. Unlike GCC, a missing response file is **not** passed
through as a literal argument: rejecting it keeps classification reliable.

`process/` resolves executables explicitly through PATH and invokes them
without a shell. POSIX uses `posix_spawn` when `posix_spawn_file_actions_addchdir_np`
is available, otherwise `fork`/`execve` (including on glibc 2.28). The fallback
prepares arguments, environment and paths before forking, uses only
async-signal-safe calls in the child, and reports setup/exec failures through
a CLOEXEC pipe. Both paths use a new process group, CLOEXEC pipes and concurrent
`poll` draining. Windows uses UTF-16 `CreateProcessW`, explicit
CRT quoting, an inherited-handle list, two reader threads and a Job Object
assigned before the suspended process resumes. Output limits apply separately
to stdout and stderr; excess output is drained and discarded. The deadline
includes waiting for descendants holding pipes open; after the direct child
exits, they get at most two seconds to finish before group/job termination.
The remaining group/job is also terminated when a successful run returns.
POSIX stops signalling a group once it has been killed or is known empty.
After job termination, Windows gives readers up to one second to finish,
then cancels blocked reads and marks the affected output as truncated.
Linux applies `RLIMIT_AS` with `prlimit` immediately after spawn, with a small
race before the limit takes effect. macOS ignores the memory-limit field
because it has no equivalent `prlimit`; Windows limits process memory in
the job. No signal handler or global process registry is installed.

`probe/` provides pure parsers for verbose include search lists and `-dM`
macros, identifies GCC/Clang/Apple Clang, infers the effective dialect, and
drives bounded compiler invocations for search, triple, macros, special builtins
and startup-header provenance. Search entries retain order, kind,
framework status and a GCC-internal flag; none are removed. Macro replacement
text retains every byte after the single separating whitespace character.
Probes forward every effective compile option (including `-O` and `-pthread`),
optionally replace local macros, and pass only `-m32`/`-m64` to `-dumpmachine`.
The search-list invocation excludes user include paths, which `buildIncludeSearch`
inserts separately; macro/special/startup probes keep the configured paths.
Each invocation
has a 30-second deadline and 4 MiB per-stream cap. Unique temporary directories
default to `std::filesystem::temp_directory_path()` (or `ProbeOptions::temporaryRoot`)
and are removed on success and failure. Operational entry points return `Result<T>` with an
error message; access `value()` only after `ok()`.

Build and test locally (at most two build workers):

```sh
cmake -S . -B build -G Ninja && cmake --build build -j2 && timeout 120 build/tests/unit/distill_unit_tests
```

Use `--filter substring` to select tests. `ctest --test-dir build
--output-on-failure --timeout 120` is the portable equivalent used in CI.
The helper executable supplies process-test commands on every platform.
Configure with `-DDISTILL_FORCE_FORK_SPAWN=ON` to exercise the POSIX fallback;
Linux CI runs the whole suite in both modes.
Fixtures are checked out byte-exact via `.gitattributes` and read in binary mode.
`tests/unit/fixtures/probe/gcc15-*.txt` were captured with local Ubuntu
g++ 15.2.0 on AArch64 (`-xc++ -E -v`, `-xc++ -dM -E`, `-dumpmachine`) on 2026-11-15;
files with `synthetic` in their names model other hosts. The real probe test
uses `g++` from PATH and skips if it is absent (on macOS that name may identify
Apple Clang). `spike/` remains an independent project.

`lex/` scans preprocessing tokens and directives without evaluating conditions.
Logical lines retain original byte ranges and physical start/end lines; tokens
retain their original offsets and physical line numbers. Backslash-LF and
backslash-CRLF splices, including GCC horizontal-whitespace splices, are removed
before tokenization, including in comments and ordinary literals. Raw strings span lines and their closing delimiters are
recognized in the original bytes (phase-2 splices inside raw contents are
reverted by C++; GCC restores whitespace splice gaps as one space). Newlines
inside raw tokens and block comments remain within their logical line,
retaining trailing macro replacement tokens and ignored directive tokens.
Ordinary unterminated quotes stop at newline, so skipped branches remain
scannable. Only a file-initial UTF-8 BOM is ignored. `%:` at
directive position is rejected with a diagnostic; ordinary digraph tokens are
scanned. Identifier collection excludes comments, header names and literals.

`names/` embeds `data/names/standard-headers.txt` and `standard-macros.txt` at
CMake configure time, with automatic reconfiguration when either file changes.
No runtime data files are needed. The tables cover C++ headers through C++26,
former standard headers, C23 headers and the specified libstdc++ extensions.
Macro names deliberately overapproximate, including C functions which C permits
to be macros, optional IEC 60559/Annex K names, and historical macros. Numeric
integer-width families also recognize implementation-provided widths beyond
8/16/32/64. `NameLists` embeds generated extension sets keyed by library; callers can
replace them for synthetic fixtures. Without selected libraries the union applies.
An empty or absent selected list means protection is unavailable; the union is
also unavailable if any participating list is empty. Linux profiles explicitly select both `glibc` and `libstdc++`;
a missing part disables protection. Reserved identifiers contain `__` or start `_A`–`_Z`.

`inline/` exposes `buildIncludeSearch`, `lookupHeader` and `inlineSources`.
Search directories retain command spelling and are deduplicated by filesystem
equivalence (canonical paths as fallback); `-I` paths matching system directories
are suppressed from the user chain. Quoted searches start next to the actual
includer, then `-iquote`, then the angle chain. GCC-internal probe directories
are included as system directories. Framework search entries use
`Name.framework/Headers/header`. Absolute existing includes are user headers
unless their resolved path lies beneath a system directory; missing absolute
angle names stay, and missing quoted names fail. Relative searches classify by
the directory through which they were found, including next-to-includer lookup.

GCC filename spellings are retained separately from filesystem identities:
`main.cpp` finds `near.h`, `./main.cpp` finds `./near.h`, `-I ./lib` finds
`./lib/a.h`, and `-I lib/../lib` finds `lib/../lib/a.h`. Nested quoted names
use the includer's spelled parent, retaining dot components. Markers escape
backslashes and quotes. Each opened spelling has its own user-file metadata
record (including the main source), with identity, byte size and render count.
System include metadata is per occurrence, including skipped branches.

Discovery reads every reachable user file before rendering, including inactive
branches. This makes later identifier collisions, `#undef`s and macro-stack
operations visible to recursion checks. Guard hashes use FNV-1a over device,
inode (Windows volume serial/file index) and salt as fixed little-endian 64-bit
integers. The salt starts at zero, and collisions with input identifiers or
other generated guards are retried. Recursive includes require a whole-file
guard without an outer `#else`/`#elif`, the first inner directive defining it,
and all design §4.4 protections. Nonrecursive repeated includes are copied.
The include depth counts the main source as level one. Output limits include
markers and generated guards. Each input file also has a 64 MiB read ceiling.
Input bytes are not charged against the output limit:
a long directive with whitespace can legitimately produce a smaller output.

Unchanged source spans are copied as bytes, including CRLF and missing final
newlines. Splices add the newline needed to delimit generated directives;
file-initial BOMs are removed before generated markers. Replacements preserve
prefix comments and drop the rest of the edited directive's logical line,
including suffix comments and extra include tokens (which GCC ignores).
Resynchronization markers after edits and conditional switches/ends follow the
whole logical line, so multiline comment suffixes cannot swallow them or turn
ignored tokens into live code. Every enclosing group containing an edit is resynchronized, even when the edit was skipped. The line
map has one entry per output physical line; generated markers/guards have no
source location, and original spans retain their source location.
`source_location` is rejected as an identifier anywhere outside comments and
literals: this intentionally overapproximates `std::source_location`.

Run both suites (each has a CTest hard timeout of 120 seconds):

```sh
cmake --build build -j2
timeout 120 build/tests/unit/distill_unit_tests
timeout 120 build/tests/runner/distill_inline_cases
ctest --test-dir build --output-on-failure --timeout 120
```

The case runner accepts `--filter substring`. Fixtures live in
`tests/cases/inline/<name>/`: `cmd.txt` uses shell-style token quoting without
shell expansion; `expect-error.txt`, when present, is an error substring.
Comment headers beginning `#` are ignored in these two metadata files.
Inputs are copied byte-exact to a temporary directory under the runner's cwd;
`raw.cpp` goes in a sibling directory, without user include search paths.
The runner uses D2a's process/probe components, compares GCC preprocessor token
spellings and every token's file/line, then builds/runs cases with `main` and
compares stdout and exit/signal status. GCC marker flags do not advance the
logical line, while blank source lines do. Normalizer self-checks verify token
boundaries and detect changed filenames/line numbers. Set `DISTILL_TEST_GXX`
to a GCC executable (e.g. Homebrew `g++-15`, or MinGW `g++.exe`); unavailable GCC
or Apple's `g++` skips compiler checks with a message, retaining inliner checks.
POSIX alias fixtures create real symlinks; Windows uses a second dot spelling
without requiring symlink privileges. Collision fixtures derive the candidate
guard from the copied file's actual identity.


`profile/` parses and validates judge profiles without reading the host environment
or executing a compiler. `parseProfileToml` is explicitly a **TOML subset**, not
a general TOML implementation: flat bare/quoted keys, basic and literal
single-line strings, the escapes `\\`, `\"`, `\n`, `\t`, `\uXXXX`, `\UXXXXXXXX`,
signed 64-bit integers (decimal, or unsigned `0x`/`0o`/`0b`, with TOML digit
separators), booleans, string arrays with comments/newlines/trailing commas, and
single-line inline tables of strings without trailing commas. LF, CRLF and a
file-initial UTF-8 BOM are accepted. Tables, dotted keys, dates as values, floats,
multiline strings, additional escapes, duplicates, invalid UTF-8 and raw control
characters are errors with file/line diagnostics. Schema validation rejects
unknown keys and wrong types, validates calendar dates, HTTP(S) provenance URLs,
macro spellings, compiler versions, supported numeric dialects (98/03 through 26),
libraries and snapshot filenames. `std` omits the `-std=` prefix. C++26 currently
uses GCC's provisional `202400L` value; a finalized dialect needs an update.

`Profile` keeps the declared target/compiler/dialect, ordered `flags` and macro
operations, `link`, `inert`, `build`, `snapshotCompiler` (TOML `snapshot_compiler`),
selected `libraries`, check compilers and file/date/source provenance. Obsolete `defines`, `library` and `allowed` keys
are rejected. Profile flags use the command classifier; rejected options cannot
be declared inert and `-std=` belongs in `std`. `-fmodules` and `-fopenmp` are
modelled options because they affect macros. Explicit `-D` and `-U` operations
preserve order, empty replacement text and function-like definitions.

`initialState(name)` returns a four-state `MacroFact` with optional definition
and provenance: compiler builtin, startup header, command operation, special
builtin, native model, A5, A4 or approximate allow-list. Reproduced mode requires
a matching named build, affirmative snapshot provenance and a complete embedded
compiler-version registry. `snapshot_compiler` records the actual alternate
compiler's exact package, recipe, digest-pinned Docker Hub image, Homebrew
core GCC bottle blob or official WinLibs release archive blob (snapshot only;
`image`, `homebrew` and `winlibs` are forbidden in
`build`); its matching `snapshot-compiler` header is
required. A `reproduced: yes` snapshot without a profile `build` is rejected.
Approximate mode uses only the commented data allow-list
for snapshot facts; explicit command operations remain known. Unlisted names use
A5 (non-reserved), A4 (reproduced reserved), or remain unknown. Special builtin
availability and invocation are distinct: `activeSpecialBuiltin()` follows the
current binding; a command definition is ordinary, and a final `-U` is undefined.
`checkA3` checks effective definedness and reports all known conflicts. The raw
`predefined` table includes every known snapshot definition; callers must consult
the fact provenance when applying A2(a), rather than treating startup-header
definitions as compiler builtins.

`loadProfile` reads a file and its sibling snapshot. `lookupProfile` treats a
separator or `.toml` suffix as a path; otherwise the injected user directory
precedes embedded data by matching full name or alias; identifier collisions
between user profiles are errors and bundled shadowing warns on every run.
`listProfiles` supplies the catalog for `--judge list`. The real home/environment
is never inspected implicitly.
An invalid override or missing named snapshot is an error. CMake embeds profiles,
snapshots, the approximate allow-list, special registries and native models;
adding/removing data triggers reconfiguration. The Linux fixture is a real local
GCC 15.2 capture with approximate provenance. The reproduced MinGW fixture is
explicitly synthetic and tests format/fact semantics, not a real judge inventory.

Snapshot format and research are specified completely in
[`data/judges/README.md`](../../data/judges/README.md). The loader validates the
versioned header, effective argv, target/compiler/dialect, per-name facts,
startup/header provenance, complete special probes and native-model coverage.
`ci/profiles/snapshot.py` captures the full profile command, uses separate line
markers to detect startup headers, probes every registered special name, and
joins supported native configurations after all options and macro operations.
Unknown startup headers or variation outside the native model fail closed.
The profile workflow installs the exact MSYS2 package/dependency set or builds
AtCoder GCC from its published configure recipe, then uploads review artifacts.
Codeforces has no published package revision: its `snapshot_compiler` selects
available MSYS2 GCC 14.2.0-3 and the snapshot remains approximate. The installer
checks the pinned dependency closure against the live listing and each package's
`.PKGINFO` depend/provides records before installation. The shipped CI snapshots
are exercised directly by embedded-profile tests; run provenance is recorded in
`data/judges/README.md`.

Generate A1 inventories with Python 3.11+:

```sh
python3 ci/extensions/scan.py --platform linux --compiler g++ --output-dir out/extensions --headers-version 'glibc/GCC versions'
timeout 120 python3 -m unittest discover -s ci/extensions -p 'test_*.py'
timeout 120 python3 -m unittest discover -s ci/profiles -p 'test_*.py'
```

The generator starts from all available standard-header-list entries and every
header under `ext/pb_ds/`, follows textual include/include_next operands in both
active and inactive regions using the compiler's `-E -v` search order, and scans
only reached files. Computed includes are not followed, a limitation recorded in
every generated list. Files are attributed separately to glibc, libstdc++,
MinGW-w64 or macOS SDK directories; compiler intrinsic headers are traversed but
not attributed to a library. Version-2 scans replace broad inventories; workflow
merging unions only reachable-header inventories across compatibility versions.
Reserved and standard names (including numeric width families) are excluded.
The shipped merged CI lists contain 1505 glibc, 167 libstdc++, 409 MinGW-w64
and 600 macOS libc names. `NameLists` and `InlineOptions` take a list of libraries:
protection requires absence from every selected set and fails closed
if any selected set is empty/missing. With no selection all libraries apply.

`resolve/` implements D2d without LLVM. Call `resolveConditions(raw, command,
evaluator, options)` with the `InlineResult` from `inlineSources`; the result
contains flat text, its physical-line map, disabling reasons, per-directive
records, `unknownInUserCode` and counts keyed by decision/reason. Raw locations
are one-based; original locations are optional for generated once guards.
Generated guards still count as user code for the pruning precondition. Records
include `#else` and `#endif`: an else records alternative reachability; an endif
is true for a resolved block, unknown for a retained block, or dead when enclosing
code was dead. Conditions after a selected alternative are dead, and elif after
an unknown condition is unknown. Counts include these structural directives.

Macro state uses `MacroFact` and the probe's `Macro` definition type. Source,
A3, header, pragma, join and no-profile provenance supplement profile provenance.
Source definitions retain the full preprocessing-token replacement list, with
whitespace normalized only in the isolated evaluation copy; original definitions
in flat text retain their exact bytes. Parameters (including named and standard
variadics) are bound names. Closure analysis deliberately overapproximates unused
arguments and function-like macro bodies. Macro-generated `defined` is unknown
because its expansion has implementation-dependent/undefined behavior. A paste
in a reached replacement list blocks resolution, but a definition used solely as
a `defined` operand does not inspect its replacement. Unknown alternatives start
from the entry state; joins compare bindings, combine provenance for equal
bindings, and include the implicit empty alternative of a block without else.
Standard includes preserve positive compiler/special/native predefinitions under
A2(a), including source changes to those bindings; startup-header facts have no
such protection. A missing feature model conservatively invalidates all reserved
names at GCC option pragmas. A3 also checks withheld approximate-snapshot bindings.

`EvaluationRequirements` records dialect and target integer layout separately
from external macro facts. Missing fields in options inherit from a profile;
without a profile only command dialect is available. Command-overridden
representation macros cannot establish physical integer widths or signedness. The conservative arithmetic
rule allows a missing integer model only when **every token in the condition and
its expanded closure** is an identifier, `0`, `1`, `!`, `&&`, `||` or parentheses
(after interpreting `defined`). All other expressions require the complete
integer model. Literal syntax, digit separators, alternative operators, C++
Boolean keywords and variadics also require a known dialect. This can retain
conditions whose arithmetic happens to be portable. Evaluator errors retain the
condition. A condition's record stores only the arithmetic/dialect requirements
it needed, and the external macro facts reached, with a flag distinguishing
definition use from definedness alone. Mixed joins retain external provenance;
purely derived source bindings are not external facts. `sameDecisions(a,b)` is directional: every true/false decision
in `a` must agree in `b`, or be dead in `b` because that configuration proves it
unreachable; unknown/dead entries in `a` impose no value requirement. Both runs
must have identical directive inventories and neither may be disabled.

Rendering copies retained logical lines byte-for-byte. It emits resynchronizing
`#line` markers after deletions, including at EOF and every enclosing alternative
boundary/end that contains an edit. This matters because a marker inside a
skipped alternative has no effect. The line map retains original entries for
copied physical lines and null entries for generated markers. Unknown groups keep
all their original directives; nested groups can still resolve independently.
Disabled resolution returns byte-identical raw text and map. Resolver limits
cover nesting, closure tokens and output size, with a cooperative step deadline
through discovery, walking and rendering. `ConditionInput` carries the remaining
step deadline to evaluators. The library Clang evaluator is in-process; the CLI
uses a separate bounded worker for each evaluation. CTest bounds each test binary
at 120 seconds.

`ConditionEvaluator` has two implementations. Production `distill_clang`
(`src/clang/evaluate.cpp`) is a separate optional static library; `distill_core`
never links LLVM. It invokes cc1's `Preprocessor` with `-undef`,
`UsePredefines=false`, no header search, and an empty in-memory VFS without a
real-filesystem overlay. The only source is a memory buffer. No driver, resource
headers or filesystem inputs are needed for isolated `#if` evaluation. Intrinsic
and standard bindings that `-undef` can leave active are explicitly undefined
before adding exactly the closure definitions/placeholders. Clang also enumerates
and undefines every intrinsic binding in the actual preprocessor instance. The fixed target
layouts have 8-bit char, 32-bit int and 64-bit long long/intmax: x86_64 Linux
(LP64, signed 32-bit wchar), AArch64 Linux (LP64, unsigned 32-bit wchar), i386 Linux
(ILP32, signed 32-bit wchar), and x86_64 Windows GNU (LLP64, unsigned 16-bit wchar).
Char signedness is passed explicitly. Other layouts fail closed. These are
preprocessor TargetInfo layouts, so they do not require target code generators.

`tests/support/GccEvaluator` uses the D2a bounded process runner and local
`g++ -undef -E -P -nostdinc -nostdinc++`, with the same intrinsic isolation and
result tokens. It probes and checks its local integer layout, permits explicit
char signedness/short-wchar changes, and rejects other target layouts. Its
unique source files live under an injected test directory and are removed on
success/failure. It is never linked into the product. `DISTILL_TEST_GXX` selects
the GCC binary. `DISTILL_TEST_EVALUATOR=gcc|clang|differential` selects the test
oracle; differential requires agreement on both success/error and Boolean value
for every synthetic input, including those produced by fake-logic tests.

Run resolver cases and units without an SDK:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
timeout 120 build/tests/unit/distill_unit_tests --filter resolve_
timeout 120 build/tests/runner/distill_resolve_cases
```

`tests/cases/resolve/<name>/` has `cmd.txt` with the inliner runner's quoting,
sources, `expect.txt` (`file:line true|false|unknown[:reason]|dead` for each
if/elif in traversal order), and optionally `expect-disabled.txt` with a reason
substring. SPDX/comment lines beginning `#` are ignored in metadata. `profile.txt`
selects a shipped profile or `local`. `local` captures macros and special probes
from the actual GCC under a fixed gnu++20 fixture command (`-O0 -fsigned-char
-DDISTILL_FIXTURE_JUDGE=1`), records startup-header provenance separately, and
writes the snapshot only in its temporary test directory.
`profile-gcc15-gnupp20.toml` (with an optional sibling snapshot) is also
supported; its resolutions
must be satisfied by facts freshly captured under its effective local command.
Without a profile, local fixture facts check the decisions. Builds replace local
`-D`/`-U` with profile operations, and compare raw/flat preprocessing tokens with
every file/line plus stdout and exit/signal. Shipped profiles run decisions only;
the Linux `_WIN32` regression requires a Linux GCC. `expect-mismatch.txt` marks
one deliberate A2(a) violation: both token/location and runtime comparisons must
reject flat. `--filter substring` selects cases. Copies and compiler outputs stay
under the runner cwd and are removed by RAII.

CI's unchanged `unit (...)` checks build every non-LLVM target. The additional
`resolver (linux, clang)` job authenticates the SDK release check with the job's
`GITHUB_TOKEN`, acquires `llvm-23.1.5-1` linux-x86_64, and builds/runs in the same
manylinux_2_28 environment as the SDK/spike (avoiding reliance on Ubuntu's
libstdc++ ABI). The SDK configure switch is:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDISTILL_LLVM_DIR="$PWD/spike-work/sdk/lib/cmake/llvm"
cmake --build build -j2
ctest --test-dir build --output-on-failure --timeout 120 -j1
```

CMake derives sibling Clang's config directory and enforces LLVM 23.1.5. With
the SDK, CTest adds Clang and differential runs for both resolver suites. Linking
these targets needs CI-runner memory; on the small development VM only compile
the Clang-dependent TUs with `-fsyntax-only -fno-rtti -Isrc -isystem <sdk>/include`,
one at a time, and remove SDK/build directories after verification.


`validate/` implements configurations and preprocessing comparisons. A
`Configuration` owns its exact option argv, structured command (ordered macro
operations, include paths and classified options), compiler identity/triple and
probe, observed four-state facts and option changes with reasons. Probe options
accept an injected `ProcessRunner`, timeout, output cap and temporary root.
`-dM -E` includes the complete effective compile flags; `#ifdef` probes cover the
GCC version's embedded special registry (the union is also probed conservatively
for unsupported compilers). A separate `-dD -E` run attributes `stdc-predef.h`
definitions to startup headers. Unexpected startup headers fail the probe.
A command definition overrides an operator binding; a final `-U` removes it.

Local requirements use the same `Profile::initialState` and
`profileRequirements` rules as snapshots. A complete registered local GCC has
exhaustive observed facts; a compiler without a complete registry uses the
approximate allow-list. The internal observed profile's `reproduced` field means
complete local observations, never a claim about a judge build. A3 is checked
against the actual effective local definitions before assuming a user-defined
non-reserved name initially absent. A5's local counterpart is an observation:
unlisted non-reserved names are absent from that configuration's macro dump and
special probes. Startup-header facts do not receive A2(a)'s builtin protection.
Native settings describe the concrete local host; they are never a judge fact.

`flatCommand` strips user `-I`/`-iquote` paths and controlled outputs. With a
profile it removes local `-D`/`-U` and dialect, then appends the profile dialect
and **all** profile compile flags in order. Other local compile options remain;
later profile options therefore take precedence. A check compiler uses profile
link options; the local fallback keeps local link options and records each
omitted profile link argument and why. Relative system/library paths become
absolute before generated builds change cwd. `satisfies` reruns the resolver on
raw using the observed facts and compares directionally with `sameDecisions`.
Candidates are available `check` compilers in profile order, then the local
fallback. After probing, a check candidate must pass
`compatibleProfileTriple(profile.triple, probe.triple)`; an incompatible target
is skipped before satisfaction/build checks, with the compiler and both triples
recorded in the diagnostic and report. Thus native Linux `g++` in Codeforces'
check list is skipped, and the local fallback omits its Windows link flags.
The first satisfying configuration is selected; a failed preprocess
or build check rejects flat rather than searching for a compiler that accepts it.

Without a profile, **resolution still receives no local macro snapshot or local
integer model**. It uses source facts, A3, and the explicit command dialect (or
the probed default dialect when omitted). Thus `#ifdef LOCAL` and compiler/OS
conditions remain unknown. The local configuration's own probes then validate
the decisions actually made. This preserves §4.5's no-profile rule while checking
that its dialect and A3 assumptions hold for the local compiler.

`targetEquivalent` additionally requires a reproduced judge profile, exact
ordinary-macro equality and every registered special-builtin probe, compatible
triple/compiler/dialect, the full ordered profile flags, and no host-dependent
option in either the original or validation command. Agreement only on used
facts, an approximate profile, and any `native` command give `local`.

`parsePreprocessed` reuses the D2b lexer. Numeric GCC markers (including escaped
file names, zero line numbers and flags 1–4) set the following logical location;
blank lines advance it. A scan before marker interpretation keeps marker-shaped
lines in multiline literals intact. Tokens retain their logical starting line;
pragma start/end events retain directive boundaries. User spellings map through
the inliner's filesystem identities to one original spelling; system files keep
their spelled paths, identified by marker flag 3. `comparePreprocessed` reports
the first differing token or boundary with both locations. Parser fixtures test
line shifts, spelling/boundary differences, aliases, system paths and raw strings.

`pipeline/distillProgram` is the core entry point (`distillNoPrune` remains an
inline/resolve-only wrapper): command/probe, original build,
inlining, raw preprocessing/build, resolution, configuration selection, flat
preprocessing/build, pruning analysis/rendering/invariants/build, then selection
of the checked bytes. It accepts the
command, `DistillOptions`, evaluator and process runner. It returns a
`PipelineResult`; the caller publishes `bytes` without rendering them again.
Generated files are binary writes, and their bytes are checked again after the
checks. Baseline preprocessing retains user include paths; raw/flat checks run
from a new directory without them. Unique private work directories default to
`std::filesystem::temp_directory_path()`; `DistillOptions::temporaryRoot` remains
an override for tests. They are removed by RAII, except raw failures: exit 1,
no output bytes, files kept in that same temporary directory and their path
reported. Quoted includes cannot find a missed user header beside the original
source. Flat failure, unavailable satisfying configurations,
resolution limits or disabled resolution fall back to the already checked raw.
Each phase shares a 30-second deadline across its child processes/evaluations,
including samples. Every sample run also has a 30-second per-run cap, clipped to
the remaining phase budget. Process output is capped. Inline/resolve also enforce
their structural limits.
`DistillOptions::samples` is the shared runtime API: named stdin paths,
resolved against the original command cwd. Baseline runs are compared with raw;
flat is compared with the original built in flat's configuration (restoring user
include paths for that original build); pruned is compared with flat's saved
runs. Each artifact has its own executable in the private work directory. Runs
compare stdout and exit code, allow nonzero exits, ignore stderr and fail on
signals, deadlines or output caps. Unsupported host OS/architecture combinations
report `skipped: can't run target binaries here`. Empty samples give
`not requested`. A completed comparison reports `compared N samples` in the
result line and report. A difference rejects the candidate with
`differs on sample X (distill bug or nondeterministic program)`; flat/pruned fall
back to checked raw/flat, while a raw difference exits 1, publishes no source,
and keeps the work directory. Strict returns 3 unless selected bytes are
pruned/target.

The CLI exposes this API with repeatable `--verify-run <path>` before the
compiler. A regular file (any suffix) is one stdin sample; a directory contributes
its immediate regular `*.in` files in filename order. It does not recurse.
Selections retain command-line order, including repeats. Missing/non-regular
paths and directories without `*.in` samples fail with exit 1 before pipeline
work. `collectRunSamples` implements this selection in the LLVM-free core.
`-v` prints each sample comparison and whether it matches. Stderr differences
do not affect equality, while its output remains bounded by §3's caps.

`report/` renders the result header, profile/date, option classes, assumption
dependencies, configurations and their satisfied macro/arithmetic facts, exact
process argv/cwd, omissions and failures, then every conditional directive at
its original location (generated guards use `<generated>`). D3b merges the analysis report into the main header: parse argv/omissions,
disable reasons, policy/ADL-registry digest and coverage, family fates/locations,
and unique primary/transitive family bytes. Structural input bytes (shells,
directives, trivia and generated provenance markers) have separate attribution;
these source-accounting totals are not a claim about final serialized size.
A failed candidate's analysis remains labelled as analysis and adds no A6
dependency to the checked flat fallback. A failed flat resolution's decisions remain visible alongside
its rejection, and do not certify the selected raw artifact.

`src/cli/main.cpp` is built only with `DISTILL_LLVM_DIR`; it links the static
Clang libraries and static C++ runtimes like the spike. It parses the options
through the LLVM-free `command/options` component, loads an embedded or explicit
TOML profile (and sibling snapshot), constructs a Clang worker evaluator, calls
the pipeline and writes the exact result bytes. Default source-only commands use
`g++`; an implicit Clang is rejected and an explicitly named Clang is noted.
Each worker is the same executable with a private file protocol. Its condition
files and cwd live in a separate unique private OS temporary directory, removed
by RAII, so the CLI creates no work files in the user's source directory.
The executable path comes from `/proc/self/exe` on Linux,
`_NSGetExecutablePath` on macOS, or `GetModuleFileNameW` on Windows; only a failed
OS lookup falls back to resolving `argv[0]`. Workers are launched by
D2a's process runner in a process group/Job Object with the remaining deadline
and bounded output. No LLVM resource headers are needed for isolated conditions.
Pruning is the default; `--no-prune` requests inline/resolve only. Repeatable
`--keep <name>` requests namespace-indexed names; an unknown name exits 1 with
no artifact. Keep names are validated even alongside `--no-prune`; if no checked
flat configuration/index is available they cannot be bound and exit 1.
`--strip-comments` runs before every artifact's checks. Repeatable `--verify-run`
selects samples for those checks. The CLI uses its own `--internal-prune` mode
for extraction, so its deployed binary needs no sibling worker executable. Output defaults to
stdout; progress/result go to stderr. File output is staged in a private sibling
directory, then atomically renamed over the destination after checks (Windows
uses MoveFileExW replacement). Stdout receives bytes only after all checks.

```sh
# SDK builds only (link/run on CI, not the small development VM):
build/distill --judge codeforces --report report.txt -o submit.cpp g++ main.cpp
# Core pipeline cases, without LLVM:
timeout 120 build/tests/runner/distill_pipeline_cases
```

`tests/cases/pipeline/<name>/` has `cmd.txt`, sources and `expect.txt` key/value
metadata: `artifact`, `verification`, `exit`, `configuration`, optional `profile`,
`strict`, report/reason substrings, platform requirements and fault injection.
SPDX/comment metadata lines begin `#`. `profile=fixture` captures a valid TOML
profile and sibling snapshot from the actual test GCC under gnu++20, `-O0`,
`-fsigned-char`, `-DDISTILL_FIXTURE_JUDGE=1`. It exercises `target` for supported
registry versions, otherwise conservatively expects `local`. It is test
provenance about this compiler only. Captured compiler banners normalize CRLF to
LF before escaping newlines in the snapshot's single-line `compiler-version`
header; snapshot record separators accept both LF and CRLF. Pipeline fixtures
are checked out byte-exact, including the CRLF source case. Work files, reports,
published artifacts and worker input files use binary I/O; Windows CLI stdout
and stderr are binary too, preserving both artifact bytes and the worker's exact
`true\n`/`false\n` replies. `check=present` tests a found check compiler;
the default fixture lists an absent check compiler and tests fallback.
`check=wrong-target` puts a compiler shim on PATH that reports a MinGW triple
for a Linux fixture. It must be skipped and reported before the local fallback
produces `flat (local)`, omitting the deliberately incompatible profile link flag.
The missed-inline hook restores a quoted user include, with matching headers
beside the source and under its `-I`/`-iquote` path; raw must fail without those
search paths and retain files in the temporary root. Cases verify generated
argv/cwd, the core temporary-root override, successful cleanup and reported
retention, then clean up retained test files. The resolver hook corrupts a
return token in flat **after**
correct decisions, so satisfaction alone passes but preprocessing rejects it.
Hooks use the injected process runner; the CLI run uses a test compiler shim
with the same mutations, with no product testing backdoor.

All required unit jobs run the LLVM-free pipeline cases. The unchanged
`resolver (linux, clang)` job also builds `distill`, runs those same cases through
the real binary (`distill_pipeline_cases --cli <binary>`), runs core pipeline
cases with Clang evaluation, and checks help, default commands, strict exit and
option errors, custom `argv[0]` and symlink invocation. An LLVM-free process test
also checks OS executable discovery when a child receives an unusable `argv[0]`.
Local verification never links LLVM: syntax-check `src/clang/`,
CLI and Clang-enabled test support one TU at a time against the published arm64
SDK, then delete the SDK and build trees.

`prune/` supplies D3a's LLVM-free analysis boundary. `PruningModel` owns the
immutable checked flat buffer, physical edit ranges and line-map provenance,
indexed namespace names, callable/dependent names, explicit AST and association
edges, union obligations, keep annotations, system-entity paths back into user
families, macro-expansion facts and all disabling reasons. `computeRetention`
validates the model, applies the preconditions and never-removed policy, then
computes rules 1–5 to a fixpoint. Union-find joins retention obligations without
joining source ranges. Every fate has a primary reason, immediate cause and
initial cause; disjoint family bytes have primary and transitive attribution.
Removed families report “outside the retention closure”. A6 is recorded for
negative removal reasoning even if no ADL root was encountered. Invalid models
are errors; failed preconditions keep every family and return every disable
reason. Names in shells and directives participate independently of AST edges.
`staticNameInvariant` checks supplied rendered text against removed indexed
names, excluding comments/strings and including literal-operator suffixes.
D3b runs this invariant on the final rendered candidate before its build.

`names/adl` loads embedded `data/names/adl.txt`, with an explicit policy version,
per-dialect rows and a build-time SHA-256 digest. C++11/14/17/20/23 and their GNU
spellings are covered. Rows identify lookup mechanism, stable normative clause,
public entry and verification status. The fixed sources are N3337, N4140, N4659,
N4861 and N4950;
public CPO-only over-retention is distinguished from a same-named ADL hook.
The pre-C++20 surface is `swap` ([swappable.requirements]), range-for `begin`/
`end` ([stmt.ranged]), the sixteen valarray functions ([valarray.transcend]),
and conservatively `make_error_code`/`make_error_condition`; C++17 adds
structured-binding `get` ([dcl.struct.bind]). There are no CPO rows before C++20.
The audit searched each fixed draft's library text for ADL/unqualified lookup
permissions; operator calls, callbacks, members and template specializations
follow their separate retention rules. Other library calls are qualified by
[contents]/3. The eight C++11–20 error conversion rows are retained but marked
UNVERIFIED for exact ADL permission; N4950 [contents] supplies that permission
explicitly. Later defect reports are not silently applied. GNU extension
eligibility is separate: `extension-coverage.txt` records the incomplete GCC
14.2/15.2 PBDS spot audit and explicitly declines coverage, as for rope and
bits/extc++.h. Those includes disable pruning. Data are embedded at build time.

`preparePruning` derives a distinct cc1 parse argv from the selected satisfying
`Configuration`, preserving its triple, dialect and ordered macro operations.
GCC-internal search directories are replaced with embedded Clang resource
headers in the probed search order. User search paths remain absent. Clang keeps
its own predefinitions, including the driver's GNU compatibility default
(`-fgnuc-version=4.2.1`), which cc1 does not enable automatically. Untranslated
options are recorded; semantic options without support, including
modules/OpenMP/CPU tuning and `-Ofast`'s implied fast-math policy, disable pruning. Source
pragmas use the audited allow-list below; other pragmas disable pruning.
Generated markers are identified from D2's null line-map entries, never by
spelling alone. Keep comments are captured as physical declaration-token offsets by
`captureKeepOffsets` before any D3b comment stripping, then bound to families
in the child. `keepOffsets`/`annotations` survive independently of the comment.
D3b maps the declaration offsets through resolution and rendering before extraction.

The cc1 argument audit compares `clang++ -### -std=gnu++20 --target=<triple>
-fsyntax-only` (also strict `c++20` and the MinGW target) with LLVM 23.1.5's
`Options.td`/`LangOptions.def`. Driver defaults cannot simply be inferred from
the initial values in `LangOptions.def`: option marshalling can replace them.
The parsing policy is:

| cc1 option | Reason and handling |
| --- | --- |
| `-x c++`, `-std=…`, `-triple …`, `-fsyntax-only` | Parse the selected dialect/target without code generation. Driver `--target` becomes cc1 `-triple`; x86 `-m32`/`-m64` adjusts that triple. |
| `-fgnuc-version=4.2.1` | Reproduce Clang's driver compatibility macros (`__GNUC__`, etc.); bare cc1 otherwise leaves them absent. This does not initialize builtin function names or impersonate the selected GCC version. |
| `-fcxx-exceptions`, `-fexceptions` | Enable both C++ exception syntax and exception handling/feature macros, as the GCC/Clang C++ drivers do. Both cc1 defaults are off. An explicit selected `-fno-exceptions` leaves both off. |
| RTTI | cc1 defaults to RTTI on for C++, matching GCC. Forward an explicit selected `-fno-rtti` only. The SDK's `-fno-rtti` used to compile the worker is independent of the input's language options. |
| `-fmath-errno` | Require math errno semantics, matching GCC defaults and the Linux/MinGW driver vectors; cc1 option marshalling defaults this off. It also controls `__NO_MATH_ERRNO__` and can select header branches. |
| `-fdeprecated-macro` | Define `__DEPRECATED`, matching the C++ driver default and GCC's ordinary C++ mode. Bare cc1 leaves it absent; libstdc++ uses it to select deprecated declarations. |
| `-nostdinc++`, `-nostdsysteminc`, `-nobuiltininc`, `-resource-dir`, explicit include flags | Suppress implicit host searches and install the probed GCC headers plus embedded Clang resource headers in order. `-nobuiltininc` only disables automatic resource **header search**, not builtin function registration. |
| `-D`, `-U` | Preserve macro operation order. |
| Character signedness | LLVM 23 cc1 defaults `CharIsSigned` to true on **all** targets; the driver supplies unsigned target defaults. Emit only `-fno-signed-char` for an unsigned selection. A signed selection emits neither character flag, even on ARM. Use the selected probe's `__CHAR_UNSIGNED__` for the default and the last explicit `-fsigned-char`/`-funsigned-char` for overrides. Neither driver spelling is forwarded. |
| `-fwchar-type=short`, `-fno-signed-wchar` | Translate driver-only `-fshort-wchar` to both cc1 options, reproducing unsigned short wchar semantics. |
| `-pthread` | LLVM 23's `Options.td` explicitly marks this as `CC1Option` (`LangOpts.POSIXThreads`); it is safe to forward. Clang supplies target-aware thread predefines such as `_REENTRANT` before applying ordered `-D`/`-U`. Do not replace this with a user definition at the flag's argv position. |
| `-fwrapv`, `-fno-wrapv` | Both exist as cc1 options, but remain outside the compile-command model. They are recorded and disable resolution/pruning with named reasons, preserving the existing unmodelled-option policy. |
| `-O0`, `-O1`, `-O2`, `-O3`, `-Os`, `-Og` | Forward in order: optimization affects `__OPTIMIZE__`, `__OPTIMIZE_SIZE__` and header branches even for syntax-only parsing. Although cc1 accepts `-Ofast`, it lacks the driver's implied fast-math language options; record it and disable pruning with `untranslated semantic option: -Ofast`. |
| `-march=`, `-mtune=`, `-mcpu=` | Never forward driver CPU spellings. cc1 uses `-target-cpu`, `-tune-cpu` and target features; deriving the full target-specific driver translation (including `native`) is unsupported, so each remains recorded with a named `untranslated semantic option` disable. |
| `-fno-builtin`, `-ffreestanding`, `-undef` | Never added to the extraction invocation: keep hosted builtin semantics and Clang predefinitions. The isolated condition evaluator intentionally has a different preprocessing policy. |
| `-fcolor-diagnostics`, error limits, diagnostic formatting | Omitted: they change diagnostic presentation, not parsing or lookup. |
| `-stack-protector`, `-mrelocation-model`, PIC/PIE flags | Do not copy a host driver's distribution/target code-generation policy. Stack protection defaults off in cc1; relocation does not initialize builtins. SSP/PIC/PIE feature macros can differ, so they are not claimed to reproduce the selected GCC's macro snapshot; user expansions still undergo the existing parse/judge fact checks. Explicit unsupported semantic options disable pruning. |
| Backend, debug, unwind, frame-pointer and tuning flags | Omit driver implementation/code-generation flags. Selected unsupported CPU/ABI/semantic options remain recorded and disable pruning rather than silently inheriting the host driver policy. |

The translation table is checked as complete argv deltas in
`prune_prepare_cc1_translation_table`; SDK extraction tests also exercise signed
and unsigned ARM selections, wchar type, exception/RTTI disables, thread macro
ordering and optimization predefines. Audit references for LLVM 23.1.5:
[`Options.td`](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/include/clang/Options/Options.td)
and [`RenderCharacterOptions`](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/lib/Driver/ToolChains/Clang.cpp).

Builtin function registration is a separate frontend lifecycle step, not an
argument-vector fix. `CompilerInstance::createPreprocessor` initializes the
target builtin table but does not assign builtin IDs to identifiers.
[`FrontendAction::BeginSourceFile`](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/lib/Frontend/FrontendAction.cpp)
normally calls `PP.getBuiltinInfo().initializeBuiltins(PP.getIdentifierTable(),
PP.getLangOpts())`. Extraction calls `ParseAST` directly and must perform this
registration itself after creating the target/preprocessor and before parsing.
Without it, even `__builtin_unreachable` and `__builtin_popcount` are undeclared.
The spike's `ToolInvocation`/`SyntaxOnlyAction` runs the normal frontend lifecycle;
`evaluate.cpp` only lexes isolated `#if` expressions and never looks up builtin
functions through Sema. No builtin declarations or compatibility macros are
injected as a workaround. Model regressions parse builtin calls, exception/RTTI
syntax and driver feature macros, then separately parse `<bits/stdc++.h>` plus
`__builtin_popcount` using the native GCC probe's target and search directories.

`extractPruning` serializes the request through a strict bounded private
protocol and uses D2's `runProcess` process-group/Job Object containment to run
`distill_prune_worker`. A deadline, crash, invalid protocol or output cap disables
pruning. CTest supplies the worker's target-file path; the tests make it absolute.
The worker receives `--internal-prune <absolute-input-path>` and runs in a private
absolute temporary directory containing that binary protocol input. Its default
wall deadline is 30 seconds (shortened by any caller deadline), with a 64 MiB cap
per output stream and a 256 MiB protocol byte cap. It sets no address-space limit:
a small `RLIMIT_AS` can prevent Clang from parsing standard-library headers.
Failures record exit/signal, timeout/truncation flags, argv/cwd and the first
2 KiB of stderr. Decode failures identify a field path and protocol line/byte;
Clang diagnostics and worker-stage errors go to stderr. The Linux resolver job
reruns extraction tests verbosely after a failing CTest run.
`src/clang/prune_extract.cpp` runs only in that child: it parses with a
real-filesystem overlay and build-time embedded resource headers, discovers
complete declaration ownership, pins macro-produced declarations and directive
crossings, and traverses patterns, known instantiations, implicit code, type
locations, bases, defaults, constraints/requires, constructors/destructors,
conversions and positive references from system entities. User specializations
keep their identities; overloads, redeclarations, records/members and user
template families share retention. Namespace/linkage blocks remain shells.
Non-member dependent lookup names retain matching families across the TU,
including later declarations and hidden friends. Dependent member/qualified
lookup uses the whole-record closure described below. Opaque asm, unaudited
attributes, recovery ASTs and parse errors still disable it.

Dependent member and qualified lookup do **not** disable pruning, and need no
extra global member-name rule. For `x.size()`, `x.template f<U>()`, `M::op`,
`M::e`, `T::value` and `T::template f<U>`, the object's/qualifier's initial user
type comes from arguments named in kept user text or the AST edges of known
instantiations. Members are never removed from a kept record (§4.7). This
includes member templates and out-of-line member definitions/specializations:
separate canonical specializations explicitly union with their enclosing
record, and their member names are not indexed at namespace scope. Explicit
member-template arguments retain their types through the ordinary lexical/AST
rules. Conversion operators (including implicit conversions) and `operator->`
chains remain in the whole record, with their return-type carriers retained
by those same rules. Base lists retain the records supplying inherited members.
Nested-name chains and `typename Tr::type` follow whole-record aliases and
bases; dependent type locations are traversed too. An enum qualifier likewise
keeps its complete enumeration. Template parameters cannot name a namespace.
Induction through these carrier steps therefore retains the user declaration
that either dependent lookup can select. A user-only pasted **member** name
adds no requirement: all members already stay. External/unknown paste
provenance continues to disable pruning.

A system trait selecting a type from user inputs does not break that argument.
For `std::conditional_t<sizeof(long) == 4, A, B>`, **both** input types occur in
kept text, regardless of the locally seen choice. Other standard traits can
select nested aliases or member/conversion return types (for example
`common_type`/`invoke_result`); those types follow the carrier closure above.
A user specialization of a system trait is retained by association with its
kept user argument types, or unconditionally when its arguments name no user
type. Its output alias then keeps the next user record. Under the existing
standard-header/A6 boundary, system traits cannot conjure a disconnected user
declaration outside these paths. Thus neither dependent AST form requires
retaining every record declaring/inheriting a matching member name. Extraction
cases check member templates, explicit specializations, conversions, arrow
chains, inherited members, nested qualifiers, pasted names, target-dependent
system-trait selection and user trait specializations; same-name decoys must
still be removed. The LLVM-free closure test separately checks both candidates
without any instantiated edge to the judge's alternative.

The source pragma allow-list is `GCC optimize`, `GCC target`,
`GCC push_options`/`pop_options`/`reset_options`, `GCC diagnostic`,
`clang diagnostic`, `pack`, `message`, `warning`, and bare `once`. Names are
matched exactly; arbitrary vendor prefixes and other pragmas disable pruning.
Optimisation/target controls and their option stacks affect code generation,
not which user declarations name lookup needs. GCC/Clang diagnostics and MS
`warning` affect warning policy; `message` emits a diagnostic. `pack` affects
layout only. Every directive and its ordering survives, and a crossing family
is pinned, so deletion cannot change the persistent option/packing state.
D2 already rewrites `#pragma once` into an identity guard (the inliner tests
assert its removal); it has no repeated-include effect in the flat TU, and
direct extraction also accepts it. Operator-generated `_Pragma`/`__pragma`
directives conservatively disable in PP callbacks because their literal source
range/effects have not been audited. The top-of-file Mint/segment-tree fixture
with `GCC optimize("O3")` and `GCC target("avx2")` requires enabled pruning;
an unknown pragma fixture requires refusal.

The audited attribute classes are `WarnUnusedResultAttr` (`nodiscard`),
`UnusedAttr` (`maybe_unused`), `LikelyAttr`/`UnlikelyAttr`, `AlwaysInlineAttr`,
`AlignedAttr` (`alignas`/`aligned`) and Clang's `MaxFieldAlignmentAttr` from
`pack`. Diagnostic attributes, branch/inlining hints and layout annotations
add no symbol dependencies; their arguments still participate in AST/lexical
closure. Declaration ownership includes leading `[[...]]`/`alignas(...)`, so
removing a declaration cannot leave an attribute attached to its successor.
Inherited/implicit attributes do not expand physical ownership into an earlier
declaration; macro prefixes remain pinned. All other declaration and statement
attributes still disable unless covered by the D3d roots below.

Policy D3d-1 additionally roots the family carrying `ConstructorAttr`,
`DestructorAttr`, `UsedAttr`, `SectionAttr`, `WeakAttr`, `VisibilityAttr`,
`AsmLabelAttr` or `AliasAttr`, including redeclarations/whole-record obligations.
An alias also roots every matching user linker-symbol family, using Clang's
selected-target mangler or the declaration's asm label. An unresolved alias
symbol disables pruning. GNU asm accepts empty text and semicolon/newline-separated
direct symbol `call`, `callq`, `jmp`, `bl`, `b`, `.quad`, `.long`, `.8byte`, `.4byte`
statements; each target must resolve to a user family. Inline asm also roots its
containing family so AST traversal retains its operand expressions. Unrecognized
assembly syntax (including MS asm) and unresolved targets disable pruning.

A lexical ownership pass precedes dependency traversal. It explicitly binds
requires-expression parameters, function/prototype parameters, lambda closure
records and init-captures, plus user local declarations visited through type
locations or expressions. Nested template-template parameters retain their
existing recursive binding. Structured bindings and range-for variables in
templates belong to the enclosing family; these local names never become
namespace-indexed names. Extraction regressions exercise all these forms.

Capturing lambdas, recursive `auto self` lambdas, `if constexpr`, fold expressions,
variadic templates/packs, structured bindings and `std::function` have no disable
trigger and follow ordinary traversal. The GNU/variadic refusal is specifically
C `VAArgExpr`, GNU `StmtExpr`, `AddrLabelExpr` and `IndirectGotoStmt`, never C++
parameter packs or folds. `ordinary_templates` exercises this set, and
`typical_library` combines it with Mint/operators, a lazy monoid segment tree,
Fenwick, DSU, `using namespace std`, `rep` and the two GCC pragmas, requiring
unused `SparseTable` removal and retention of every used structure.

Implicit Clang declarations for common scalar builtins can carry the user
call-site location. They bypass the otherwise valid “unowned user dependency”
refusal only for a finite compiler-recognized allow-list: `__builtin_popcount`,
`clz`, `ctz`, `ffs`, `parity` (each with `l`/`ll` variants),
`__builtin_bswap16/32/64/128`, `__builtin_expect`,
`__builtin_expect_with_probability` and `__builtin_unreachable`. These operate
on scalar values/control flow and introduce no further user lookup; argument
expressions and conversions are still traversed, and external C runtime
definitions already stay as roots. This exemption applies only to implicit
builtin declarations, after checking for an owned user declaration. It does
not excuse arbitrary builtins or explicitly declared user functions.
`ordinary_builtins` covers the common forms.

Static/thread storage roots use a positive syntactic initializer/type classifier,
not a constant evaluator: calls (including constexpr calls), mutation, classes,
deduced/template/member types and non-whitelisted system macro origins are not
certified inert. Unrecognized alias/initializer forms over-retain. Ordinary
locals have no independent root. PP callbacks observe synthesized/rescanned
predefined names and source overrides; token watching also detects a judge
predefinition absent from Clang. Pasting is refused if an enclosing invocation
contains external macro expansion; this intentionally overapproximates operands.
`report/prune` supplies the analysis section merged by D3b into the pipeline
report. It retains policy/registry identity, coverage, parse argv/omissions,
reasons and byte attribution independently of whether the candidate passed.

All required core jobs run the hand-built pruning unit models, including an
exhaustive 4,096 graph/root closure table and worker-protocol failure cases.
The unchanged `resolver (linux, clang)` job additionally builds the extraction
worker and runs `prune_extract_models` (29 model/ownership cases) and
`prune_extract_cases` (42 cases) under CTest's 120-second timeout. Cases
live in `tests/cases/prune/<name>/`: `main.cpp` is the immutable extraction
input, `expect.txt` contains `kept <qualified-label> | <reason substring>`,
`removed <qualified-label> | <reason substring>`, or `disabled | <reason>`.
Optional `generated-lines.txt` marks physical lines whose null provenance was
produced by D2 (the direct extraction fixtures deliberately bypass the inliner's
source-#line rejection). The runner probes an effective local GCC configuration,
uses its facts as test judge facts, launches the bounded worker per case and
checks retention/refusal directly. It prints unique kept/removed family bytes,
including the Mint/operator measurement. This suite does not render or compare
pruned execution; those adversarial checks belong to D3b/D3c.

```sh
# LLVM-free, from the worktree; keep temporary files inside the build directory:
mkdir -p build/tmp
TMPDIR="$PWD/build/tmp" ctest --test-dir build --output-on-failure --timeout 120 -j1
# CI SDK build only:
ctest --test-dir build-resolver -R prune_extract_cases --output-on-failure --timeout 120
```

`render/` is LLVM-free. `renderSource` consumes the immutable checked flat model
and the retention result, validates deletion boundaries, and copies retained
spans in source order. A whole logical line disappears only when all tokens are
deleted and no outside comment survives; partial lines retain separators and
physical newlines. Adjacent physical ownership partitions are considered
together, while a split token/comment or source-directive intersection rejects
the candidate. Comments outside deletions remain unless stripping was requested.
Generated markers inside a removable family do not pin it: pruning reconstructs
markers from D2 provenance at lexer-certified logical-line starts, outside raw
strings, comments, splices and directives. Untouched CRLF bytes and missing final
newlines remain untouched. Physical offset mappings cover retained family
ranges, keep offsets/annotations, macro expansions and directive/marker ranges;
removed occurrences disappear and ranges may split around new markers.

Generated once guards keep null D2 provenance. The inliner establishes the
guarded original filename before their opening directives, including for the
root file, and restores source coordinates afterward. The resolver recovers
their presumed coordinates from lexer-recognized generated markers, ignoring
marker-shaped raw-string contents. Neither resolution nor rendering invents
raw/flat/pruned filenames for generated lines. Raw contains full once guards;
flat drops resolved guard conditions but retains their defines to suppress
later inclusions. Pruned preserves these harmless defines as directives.

The rendering invariant re-lexes actual output independently of that mapping.
It compares every surviving token's spelling/kind and logical file/line, and
source-directive start/end boundaries and order. Directive tokens share their
logical start for this comparison: multiline directive comments may contract,
but the replacement token list and the following source line remain intact.
The first discrepancy names both tokens/locations. The static name invariant
then checks the final text against removed namespace-indexed names. Neither
invariant is a runtime-equivalence proof or expands pasted identifiers.

Stripping uses comment spans recorded during the D2 lexer's phase-2 scan, so
strings, raw contents and header names cannot impersonate comments, including
spliced delimiters. Outside directives, a comment becomes one space or its
physical newlines. Inside an unconditional directive it becomes one space and
the next logical line is resynchronized. Multiline comments in conditional
directives stay: the following marker may be skipped and cannot repair a later
`#elif __LINE__` expression. A comment also stays when exposing its newlines
could introduce a directive. Raw/flat stripping preserves existing generated
markers; pruning reconstructs them. Keep comments are captured before stripping,
carried by physical declaration occurrence through resolution (dead occurrences
are dropped), and supplied to extraction from checked flat provenance.

`DistillOptions::pruningWorker` identifies the bounded extraction worker; the
CLI supplies itself. Analysis uses exactly the selected satisfying flat parse
configuration. Disabled analysis leaves checked flat intact and reports every
reason. Otherwise rendering, both invariants, build and optional sample comparison
must pass. A rejection returns the already checked flat bytes without rendering
again. Policy D3b-1 includes a narrow location-intrinsic exception: intrinsic
`__LINE__`/`__FILE__` rely on the independent location invariant rather than
constant macro-definition equality. Explicit definitions still undergo the
ordinary macro check; external token-paste operands still disable pruning.

Required core jobs run byte-exact rendering/stripping tests, 506 separator pairs,
and synthetic-worker pipeline regressions exercising publication bytes, keep
mapping, strict status, disabled/render/build/run fallback and unknown names.
SDK-only cases in `tests/cases/pipeline` use `prune=yes`, `strip=yes`, `keep`,
`contains`/`absent` and test-only `fault` metadata. `--worker <path>` enables the
real extraction path and internal sample comparisons. `--cli <binary>` uses the
real CLI and independently builds/runs its exact output against a real-CLI flat
artifact. D4a passes `--verify-run sample.in` for sample-enabled CLI cases,
including the forced pruning runtime difference and constructor/destructor roots.
The existing compiler-boundary fault hook exercises CLI fallback to flat.
Fault hooks remain in the test runner, without a product flag. Every case
continues after failures with its complete diagnostic.
The unchanged resolver CI job runs these plus the existing extraction suites;
its diagnostic rerun includes pipeline and CLI cases as well as extraction.

D3c adds 13 extraction fixtures (42 total) and 22 pipeline fixtures (61 total),
including seven checker fixtures. They cover structured-binding `get`, comparison
and error-conversion hooks, chrono `from_stream`, valarray mathematics, formatter,
tuple and range-variable specializations, trivia-only mentions, overload families
and shared-line `__LINE__`. Both positive and negative specialization associations
are asserted. An unused overload remains with a used overload: this is a known
size cost of treating the overload set as one family. Extraction expectations
apply their fate to **every** family with the requested label, so one retained
overload cannot hide an incorrectly removed sibling.

`library_features.h` probes `<format>` and chrono parsing independently of fixture
sources. Missing local APIs skip only their pipeline builds. Extraction still
parses the actual user customization code against minimal standard-interface
headers in `tests/support/pruning/`, inserted as system headers only for that
probe's missing feature. These models use specified/reserved names and the
specified `from_stream` ADL mechanism, staying within A1–A6. Model results are
labelled `MODEL`; they do not certify an implementation of the missing library.
The measurement corpus always uses the real system headers.

`tests/runner/checker_cases.h` is a test-runner-only hook. It copies the completed
retention result, asserts the chosen family was retained, then forces its entire
union component out. Independent probes run the rendering and static invariants,
then compile forced bytes through a second pipeline run. The runner restores the
source file immediately after that compiler invocation, leaving the forced
executable for the pipeline's internal sample API to compare. This isolates the
build/run probes from the separate checked-file tamper guard. No product flag,
environment override or production pipeline callback was added.

`checker=` in expect.txt names `build`, `static-invariant`,
`rendering-invariant`, `run`, or `none`; the comment explains why. Checks are
independent: the called-function fixture also fails the static invariant, while
the unused macro's name must pass build/run and fail the static invariant.
Rendering fixtures simulate an extra deleted token and a shifted logical line.
The constructor fixture compares stdout on an empty sample. The LP64 iterator
fixture demonstrates a local limit despite its A6 retention obligation; the
pasted branch helper demonstrates a sample limit, with a separate `x` witness
that must fail runtime comparison. Neither limit violates A1–A6.

`distill_pipeline_cases --checker-models` runs these checker mechanisms without
LLVM using explicit, fixture-owned `model.<label>=<source fragment>` family
ranges. These synthetic models root their families before forced removal; they
do not establish extraction correctness. SDK tests use actual extracted models
and actual retention reasons. D3c SDK cases have `milestone=D3c` and run with
`--d3c` in separate CTest entries to keep each test under 120 seconds. As before,
every failing case is reported and the runners continue; full bounded worker
and compiler stderr is retained for failure diagnostics.

`distill_measure --measure <worker> <source-or-directory> <table.tsv>` runs the
pipeline without sample execution and records one row per solution: selected
artifact/verification, raw/flat/attempted-pruned bytes, unique family bytes by
primary rule (1–5), separate structural bytes, pruning-disable reasons
and diagnostics. An unreached stage is `-`, never an invented zero. Failed
baselines and unsupported inputs remain error rows; worker/protocol failures
also fail the harness. Family aggregation follows its immediate cause for primary rule attribution.
Failed candidate attribution remains analysis, not
certification of the selected fallback. There are no expected-output assertions.

`ci/measure.py` drives this SDK-only mode with one `timeout 120` invocation per
source, continues after failures, and writes TSV, Markdown and complete bounded
diagnostic logs. The unchanged `resolver (linux, clang)` job measures eligible
pruning/pipeline fixtures and `tests/corpus/caide/`, then uploads both tables and
logs as `d3-pruning-measurements`. Fault, checker and explicit refusal fixtures
are excluded from size measurements. Corpus rows may legitimately fail or fall
back; D4a runs their assertions separately from measurements.

`tests/corpus/caide/` contains all 81 directories copied from the user's GPL-3.0
caide fork, original-file hashes and documented compile/source adapters.
`tests/corpus/user/` is a placeholder for the user's library and solved problems;
its measurement remains the one outstanding D3 completion item. PBDS is still
explicitly not covered: an exhaustive GCC 14.2/15.2 transitive lookup audit has
not been completed, so inclusion continues to disable pruning.

## D3d measurement fixes

Preprocess **checks** have their own `DistillOptions::maxPreprocessOutput`,
64 MiB per stdout/stderr stream, matching the §3 inline-size limit. Ordinary
builds, executable runs and every probe (even probes using `-E`) retain the
4 MiB default. Truncation still fails the artifact. Comparisons run their two
preprocessors sequentially and retain at most two 64 MiB stdout buffers plus
one 64 MiB stderr buffer during capture (192 MiB payload, excluding string
capacity). Token comparison remains buffered: its scanner scratch space,
physical-line locations and token vectors add O(bytes + lines + tokens) memory;
the byte cap is not an RSS quota. It now reuses one scan per side and moves
token spellings instead of constructing and scanning a second full buffer.
The local suite peak including the STL fixture is recorded in `D3D-SUMMARY.md`; hostile token/line
density can require substantially more memory than that representative case.

D3d pipeline fixtures cover attribute startup/shutdown stdout equality, concept
parameters, each older ADL dialect, and `<bits/stdc++.h>` raw/flat checks.
SDK D3d fixtures have separate `--d3d` CTest entries bounded to 120 seconds;
the LLVM-free runner also executes the STL fixture. Every extraction-model
failure now includes complete bounded worker stderr as well as all disable
reasons. `ci/measure.py` retains the original corpus adapters and hashes;
`tests/corpus/caide/README.md` records per-case expected classes for comparison,
including the raw-only exception caused by the corpus's unmodelled `-v`.


## D4a caide assertions

All 81 copied cases in `tests/corpus/caide/` now have individual `expect.txt`
assertions and a per-case review in [PORT.md](../../tests/corpus/caide/PORT.md),
compared with the original numbered inputs and `etalon.cpp`. Copied source
hashes and compile adapters are unchanged. The runner shares the pipeline
metadata format; corpus `contains[.N]` and `absent[.N]` compare complete
preprocessing-token sequences, ignoring formatting and comments. Kept-record,
macro, overload/specialization-name, static-storage and static-assert differences
are explicit. Unsupported inputs assert exit 1 and diagnostics; compiler feature
alternatives are limited to the documented probe/baseline failures. SDK worker
failures cannot satisfy a flat expectation.

The port explicitly maps caide's noOptimize metadata to `--no-prune`, requested
member roots to indexed record owner `C`, and stripComments to `--strip-comments`.
Legacy caide annotations, forceRemove, repair and verifyCommand controls do not
become product options; each affected case documents and asserts its v8 outcome.
The two runtime sample cases use the original input files. The resolver
(linux, clang) CI job runs each caide assertion as its own CTest with a 120-second
timeout, full bounded command/report/stderr on failure, then keeps the independent
measurement pass. Local builds do not link LLVM; normal/fork core suites exercise
selection, equality, limits, verbose reporting, skips and fallback with injected
process/worker models. Real CLI/extraction assertions require SDK CI.


## Automatic CI retries

[`retry-unassigned.yml`](../../.github/workflows/retry-unassigned.yml) handles
completed runs of Core unit tests, Compatibility matrix, Distribution spike,
Release, A1 reachable-header extension inventories, LLVM SDK and Official LLVM
archive audit (all workflows with macOS jobs). Judge macro snapshots currently
uses only Linux/Windows. The retry workflow becomes active only once its file
exists on the default branch; it checks out trusted default-branch code.

The Python stdlib helper lists **all pages for the triggering `run_attempt`** via
`GET /repos/{owner}/{repo}/actions/runs/{run_id}/attempts/{attempt}/jobs`.
A job is unassigned only when `conclusion == "cancelled"`, `steps == []`,
`runner_name` is null/empty, and `runner_id` is null/0; all four fields must be
present. See the [jobs API fields](https://docs.github.com/en/rest/actions/workflow-jobs#list-jobs-for-a-workflow-run-attempt).
It does not infer runner assignment or queue timeout from timestamps.

Only completed runs with conclusion `cancelled` or `failure`, a known non-fork
head repository matching this repository, and `1 <= run_attempt < 3` qualify.
At least one job must be unassigned, and every other job must have succeeded;
real failures, started cancellations, skipped/neutral jobs, incomplete jobs or
missing evidence stop the retry for human review. An overall `cancelled` run
also requires at least one successful job **in that attempt**: all-unassigned
cancellations could be a user cancelling before startup and are never retried,
even after a 15-minute wait. No queue-timeout exception is inferred.

The helper skips itself, rechecks that the current run is still eligible and on
the event's attempt, then sends one
`POST /repos/{owner}/{repo}/actions/runs/{run_id}/rerun-failed-jobs` with
`GITHUB_TOKEN` (`actions: write`, `contents: read` for checkout). Per-run
concurrency serializes retry handlers. The attempt cap allows at most two
automatic reruns; a cancelled retry containing no successful job is left for a
human. Each invocation logs one JSON line with its decision and reason.
Fixture-based tests intercept every HTTP request and run without network:

```sh
python3 -m unittest discover -s ci/retry -p 'test_*.py'
```

## D4b compatibility matrix

[Compatibility matrix](../../.github/workflows/matrix.yml) is an independent,
non-required workflow. It runs on relevant PRs, every Monday, or manually:

```sh
gh workflow run matrix.yml --ref YOUR_BRANCH
```

The workflow must already be registered on the default branch; `--ref` chooses
which branch's implementation to run. No ruleset or required-job setting is
changed by D4b. The existing required unit/resolver builds remain unchanged.

| Job | Build and smoke environment |
| --- | --- |
| `matrix (linux-x86_64)` | manylinux_2_28 x86_64 GCC toolset; Ubuntu system GCC; official GCC 14.2/15.2 images; AlmaLinux 8 system GCC; Ubuntu MinGW cross GCC |
| `matrix (linux-arm64)` | manylinux_2_28 aarch64 GCC toolset; Ubuntu system GCC; native official GCC 14.2/15.2 images |
| `matrix (macos-arm64)` | Apple clang consumer on macos-15, deployment target 14.0; every installed Homebrew GCC (ensure GCC 14 or 15, installing gcc@15 if needed) |
| `matrix (macos-x86_64)` | Apple clang consumer on macos-15-intel, deployment target 14.0; every installed Homebrew GCC |
| `matrix (windows-x86_64)` | MSVC `/MT` on windows-2025; MSYS2 UCRT64 GCC as in the unit job |

Each job acquires the checksum-verified published `llvm-23.1.5-1` SDK and builds
`distill` plus `distill_prune_worker` with the spike's toolchains, pinned
CMake/Ninja and at most two compilation jobs. A separate step moves both binaries
into `isolated/` and deletes the entire SDK/download/build directory. Both moved
binaries pass the spike's dynamic-dependency and minimum-OS audits. No CMake or
CLI entry-point changes were needed: static runtimes, embedded headers, wide
Windows argv and the CLI's internal worker modes were already implemented.

The matrix CLI build explicitly sets `CMAKE_OSX_DEPLOYMENT_TARGET=14.0` on macOS.
A separate step uses `otool -l` to require `LC_BUILD_VERSION` minos exactly 14.0
for both moved binaries. The arm64 runner uses macos-15 because macos-14 is
unavailable; this checks the deployment minimum, but does not exercise execution
on macOS 14.

The moved CLI runs `--help`, no-prune and default pruning on `no_profile`,
`pruned_typical_library`, `profile_macro_replace` and `pruned_strip_keep` from
`tests/cases/pipeline`. The profile case uses explicit command facts in a native
fixture profile, without inventing a reproduced snapshot. Its resolved branch
exercises the condition worker. The typical library asserts removal of the
unused SparseTable and retention of Mint, LazySegTree, Fenwick and DSU. Stripping
asserts preservation of the keep root and removal of comments. Empty sample
input exercises `--verify-run` on the deterministic no-input solutions. Every
case uses spaces and `café` in source-directory, output and report paths, including
Windows. macOS also asserts that implicit `/usr/bin/g++` is refused as Apple
Clang. AlmaLinux reuses `ci/llvm/alma-smoke.sh --cli`, mounts only the moved
binaries/copied fixtures/harness, and runs against GCC 8 in GNU C++17 mode;
GCC 8 is a minimum-platform smoke check, outside the six-dialect parse matrix.

For each matrix GCC, the CLI parses `stl_preprocess` (bits/stdc++.h, vector,
sort, accumulate, optional and structured bindings) under `c++17`, `c++20`,
`c++23` and their `gnu++` spellings. Each result must be pruned or flat with
explicit pruning-disable reasons. A pruning analysis and nonempty parse argv
must be reported; Clang parse errors, recovery ASTs, missing workers and invalid
parse targets fail even if a flat artifact builds. Refusal reasons are recorded
verbatim; a raw result or a flat fallback without an analysis reason fails.

Linux x86_64 additionally installs `g++-mingw-w64-x86-64-posix`. For both simple
and typical-library sources it checks the shipped Codeforces profile, with
no-prune and default pruning. The compile command itself uses the cross compiler,
so even the baseline cannot execute a target binary. Reports must show a passed
flat check using the compatible `x86_64-w64-mingw32` check compiler and runtime
`skipped: can't run target binaries here`; recorded process argv must contain no
invocation of a target `.exe`. `--verify-run` requests the comparison to exercise
the skip path, rather than silently omitting runtime verification.

Ubuntu's older MinGW package may lack Codeforces' `-lstdc++exp`. In that case the
shipped profile must report the precise missing-library flat failure and raw
fallback. A separately named external build-only profile then omits only
`-lstdc++exp`, retaining the Codeforces flags, triple, snapshot and other link
options. The summary explicitly labels this adapter; it does not establish
judge build equivalence or `target` verification. Other profile/build failures
fail the job. This is an intentional compatibility check with a different GCC
version, not a reproduced Codeforces toolchain.

Each `matrix-<platform>` workflow artifact contains `compatibility.tsv`,
`compatibility.md`, per-compiler `rows.json`, full command/stdout/stderr logs,
reports, acquisition/isolation evidence, configure/build logs and dependency
audits. The same table appears in each job summary even after smoke failures;
failed rows do not prevent later dialects from running. Nine LLVM-free harness self-tests guard against false passes from parse/worker
failures, missing analyses, target execution and lost failure evidence. Run them
with `python3 -m unittest discover -s ci/matrix -p 'test_*.py'`. Each CLI invocation has
a 120-second deadline and each job has a 90-minute deadline. This workflow must
run in CI: local LLVM-free tests and script checks cannot establish SDK linkage,
resource independence, GCC/Clang parsing or platform portability.

## D5a releases and version identity

[`release.yml`](../../.github/workflows/release.yml) builds on a `v*` tag or a
manual dispatch. To exercise it from a branch without creating a release:

```sh
gh workflow run release.yml --ref YOUR_BRANCH -f dry_run=true
```

The workflow must first be registered on the default branch. Dry runs execute
all five SDK builds, audits, moved-CLI GCC smoke checks, AlmaLinux 8 checks on
both Linux architectures, archives, checksums and source-linked notes. The
entire publishing job is skipped. A branch dry run uses
`0.0.0-dev+<12-character-commit>`; a tag `v0.1.0` builds version `0.1.0`.
A non-dry branch dispatch is rejected. Publishing alone has `contents: write`;
HTTP error bodies are printed, including the previously observed release-API
403. Publishing is a separate CI action and has not been exercised locally.

`ci/matrix/build.py --release` reuses the existing matrix SDK, compiler,
platform/runtime flags and two-worker limit. It injects the identity and records
SDK acquisition checksums, checksum-verified SDK measurements/source identity,
actual Ninja compile/link commands, compiler flags and UTC build date before
isolation deletes the SDK/build/downloads. Normal matrix behavior is unchanged.
Only `distill` is built/shipped for releases: its `--internal-condition` and
`--internal-prune` modes re-invoke the same executable. The standalone
`distill_prune_worker` remains a test/matrix target.

Archives include that executable, `LICENSE`, `README.md`,
`THIRD_PARTY_NOTICES.md`, `LLVM-LICENSE.txt` and `BUILDINFO.txt`. They use
`distill-<version>-<platform>.tar.gz` (Windows `.zip`), with a same-named root
directory. `SHA256SUMS` covers exactly the five archives. Source-linked release
notes and platform JSON provenance are also retained in the CI bundle. A
publisher verifies the existing tag's commit and archive checksums, creates a
draft, uploads all assets, then makes it public; existing releases are never
replaced. Dry runs upload workflow artifacts only.

`--version` is parsed by LLVM-free core code and returns before compiler probes
or input/output work. CMake cache strings `DISTILL_VERSION`, `DISTILL_COMMIT`,
`DISTILL_LLVM_REVISION` default to `0.0.0-dev`, `unknown`, `unknown`:

```sh
cmake -S . -B build -G Ninja -DDISTILL_VERSION=0.1.0 \
  -DDISTILL_COMMIT=0123456789abcdef -DDISTILL_LLVM_REVISION=llvmorg-23.1.5
```

With the pinned SDK additionally set `DISTILL_LLVM_DIR` as described above to
build the actual CLI. It prints
`distill 0.1.0 (commit 0123456789abcdef; LLVM llvmorg-23.1.5)`.
The generated identity header is private to core; tests exercise option parsing
without linking LLVM. Release packaging requires the moved executable's exact
version output to match its provenance. License/notice hashes are recorded in
`ci/release/licenses.json`; `LLVM-LICENSE.txt` is the byte-identical upstream
`llvmorg-23.1.5/llvm/LICENSE.TXT`, including the LLVM exception and legacy text.

Run release harness self-tests with:

```sh
python3 -m unittest discover -s ci/release -p 'test_*.py'
```

macOS 14.0 minos is audited on macos-15/macOS-15 Intel; no macOS 14 execution
claim is made. Full SDK linking, portability gates and manual dispatch remain
CI-only; the local editing environment must never link LLVM.

Profile maintenance heartbeat: `profileStalenessNote(profile, today)` compares
the parsed `updated` date with an injectable UTC calendar day and returns one
stderr note only after 180 days. The default clock is `system_clock`; both CLI
selection paths print the note next to `profileSelectionLine`. This diagnostic
never changes facts, generated output or exit status.
