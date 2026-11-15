<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Core library (D2a–D2c)

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
replace them for synthetic fixtures. Without a selected library the union applies.
An empty or absent selected list means protection is unavailable; the union is
also unavailable if any participating list is empty. Linux `libstdc++` protection
includes the glibc inventory; a missing part disables that combined inventory. Reserved identifiers contain `__` or start `_A`–`_Z`.

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

`Profile` keeps the declared target, GCC identity (and whether patch was supplied),
dialect, judge definitions as both strings and a macro table, compiler-default
macro table, inventory completeness, library, check compilers and file/date/source
provenance. `allowed` supplies partial `NAME` or `NAME=value` facts only without
`predefined`; absent facts remain unknown. Snapshot loading calls D2a `parseMacros`
after removing metadata comments, preserving function-like definitions and
replacement bytes. It cross-checks GCC identity, `__cplusplus`, strict/GNU mode
and recognized architecture/OS macros against the declared target. This checks
consistency; it cannot prove an arbitrary hand-written inventory exhaustive.
Unrecognized target architectures have no invented architecture checks.
`Profile::checkA3` reports every collision between user definitions and the
snapshot/allow-list/judge definitions; callers must disable resolution on error.

`loadProfile` reads a file and its sibling snapshot. `lookupProfile` treats a
separator (either slash) or `.toml` suffix as a path; otherwise it tries the
injected user directory before `embeddedProfileFiles()`. `ProfileLookup` supplies
the environment and optional explicit directory; nothing reads the real home
implicitly. POSIX uses an absolute XDG_CONFIG_HOME, else HOME/.config; Windows uses
APPDATA. Invalid overrides never fall through to embedded data. CMake embeds all
judge TOML/snapshot files and extension lists at configure time, tracks edits,
and reconfigures on adding/removing snapshots. Shipped profiles currently fail
clearly with `snapshot missing`; the Linux unit fixture is a real local capture,
while the MinGW fixture is explicitly synthetic.

Regenerate profiles with `ci/profiles/snapshot.py`, or dispatch `profiles.yml`;
see `data/judges/README.md` for research, review and update requirements. That
workflow uses pinned official GCC images for Linux and refuses a different GCC
version when an old MinGW package is unavailable. Snapshot collection deliberately
uses only the profile dialect, as required by D2c. AtCoder's additional
macro-changing flags and exact judge build/CPU still need modelling before a
snapshot can meet the full A4 contract. Also, GCC `-dM` omits special builtins
such as `__LINE__`, `__FILE__` and `__has_include` even though `defined(...)`
reports them defined. The `exhaustive` flag currently identifies a complete
`-dM` capture as specified by D2c; the future resolver must model these special
builtins separately, rather than infer their absence from this table.

Generate A1 inventories with Python 3.11+ (no third-party modules):

```sh
python3 ci/extensions/generate.py --library glibc --include /usr/include --exclude-dir c++ --headers-version 'glibc <version>'
python3 ci/extensions/generate.py --library libstdc++ --include <gcc-c++-include> --include <target-c++-include> --headers-version 'GCC <version>'
python3 -m unittest discover -s ci/extensions -p 'test_*.py'
python3 -m unittest discover -s ci/profiles -p 'test_*.py'
```

The generator scans all files recursively, joins backslash continuations before
dropping comments, includes inactive `#define` and `#undef` names, excludes reserved
and standard macro names (including numeric width families), and unions with the
existing list. Scans retain their version/header/date provenance. The broad local
glibc scan intentionally includes installed non-C++ third-party/kernel headers;
this overapproximation only prevents resolutions. `parseExtensionList` validates
identifier spelling, sort order, uniqueness and exclusion rules. Empty inventories
are valid but never grant protection. `mingw-w64` and `macos-libc` are currently
header-only placeholders. Dispatch `extensions.yml` for GCC profile versions plus
newest GCC, AlmaLinux 8, MSYS2 CRT+libstdc++, and both macOS SDK architectures; its
final artifact merges all versions with checked-in data for review, without
publishing or committing anything. Update the matrix pins when profiles change.
