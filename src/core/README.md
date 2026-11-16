<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Core library (D2a–D2d)

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
drives three bounded compiler invocations. Search entries retain order, kind,
framework status and a GCC-internal flag; none are removed. Macro replacement
text retains every byte after the single separating whitespace character.
Probes forward modelled flags except user include paths, optionally replace
local macros, and pass only `-m32`/`-m64` to `-dumpmachine`. Each invocation
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
markers and generated guards. Input bytes are not charged against that limit:
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
compiler's exact package or recipe; its matching `snapshot-compiler` header is
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
precedes embedded data. The real home/environment is never inspected implicitly.
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
cover nesting, closure tokens and output size. Clang evaluation is an in-process
component; a future CLI must run the Clang phase in the bounded worker described
by design §3. CTest bounds the whole test binary at 120 seconds in this milestone.

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
writes the snapshot only in its temporary test directory. `profile.toml` (name
`profile`, with an optional sibling snapshot) is also supported; its resolutions
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
