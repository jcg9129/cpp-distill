# cpp-distill design

Status: **approved v9**, 2026-11-16 (user): v8 plus §4.3.1 (profile names,
aliases and the diagnostic judge probe) after Codex round J1
(`docs/reviews/2026-11-16-codex-judges-r1.md`, cited as *J1-n*). v8 was v7 plus the D3 pruning amendment
(A6, §4.7, §4.8, §4.9, §6, §7), after Codex's two rounds on the pruning rules
against v7 and D2 (`docs/reviews/2026-11-16-codex-pruning-r1.md`, `-r2.md`,
cited as *D3-1-n*, *D3-2-n*). v7 (2026-11-15) was v6 plus the §4.3 amendment
(judge command, build provenance, special builtins, host-dependent options,
A5), challenged by Codex in two rounds
(`docs/reviews/2026-11-15-codex-profiles-r1.md`, `-r2.md`, cited as *P1-n*,
*P2-n*). Opus drafted v1; Codex challenged v1–v5
(`docs/reviews/2026-11-15-codex-design-r1.md` … `-r5.md`, cited as *R1-n* …
*R5-n*). Round 5 found no blocker for D2; v6 closed its two D3 holes (§4.7:
predefined macros used as values, template-selected aliases).

`distill` turns a C++ solution that `#include`s a personal library into one
self-contained `.cpp`, removes code it can show unused, and checks the result
with a real compiler.

It is a from-scratch successor to `caide-cpp-inliner` (slycelote's tool, the
user's fork at `a8b990b`). The code is new; the knowledge comes from that
repo's `docs/redesign.md` (*caide §n*), `docs/redesign-progress.md`,
`docs/optimizer-hardening-todo.md` and its 79 test cases.

## 1. Goals, contract, assumptions

Goals:

1. **Correct output first, small output second.** The output must build and
   behave on the judge like the original sources would. Where `distill` can't
   establish a fact, it keeps code; it writes only files that passed their
   checks.
2. **The local machine is not the judge.** The user develops on macOS or Linux
   and submits to Codeforces (Windows), AtCoder (Linux) and others. Nothing
   that depends on platform, architecture, compiler or judge is decided from
   the local compiler.
3. **One self-contained binary per platform** on GitHub Releases: Linux x86_64
   and arm64, macOS arm64 and x86_64, Windows x86_64, all in the first
   release.
4. **CLI only.** Internally a library, so tests can drive it.
5. **Explainable:** every resolved or kept `#if`, every kept or removed
   declaration, and every assumption relied on is in the report.

Non-goals: removing unused system `#include`s (caide §7 item 5); MSVC as a
judge compiler; minimal output; inlining system headers; editor integration.

### 1.1 What "behaves the same" means (user, 2026-11-15)

Same stdout and exit status, for the same stdin, on the judge's
configuration. Not part of behaviour: stderr (so `assert` messages may
differ), `__DATE__`/`__TIME__`/`__TIMESTAMP__`, and the text of `__FILE__`
(`distill` keeps the user-visible file name). Line numbers *are* part of
behaviour where the program can observe them (§4.4).

### 1.2 Supported input

Rejected with an error (exit 1, nothing written):

- more than one source file; a compile command that doesn't build an
  executable, or names other inputs; options in the rejected list (§4.1);
- any computed include (`#include MACRO`) in user code (R3-5);
- `#include_next`, `#pragma GCC system_header`, `#line` in user code;
- `__has_include`/`__has_include_next` naming a header found in a user path;
- a quoted `#include` in a branch Clang doesn't parse, if the header isn't
  found locally (it can't be classified as user or system header, R3-5);
- `__INCLUDE_LEVEL__`, `__BASE_FILE__`, `__builtin_COLUMN`,
  `__builtin_source_location`, `std::source_location` in user code;
- a recursive inclusion that §4.4 can't prove to be a no-op, and any
  recursive inclusion if user code contains `push_macro`, `pop_macro` or
  `_Pragma` (R4-F).

Accepted with transformations disabled (only `raw.cpp` can be written):
`__COUNTER__`, `#pragma push_macro`/`pop_macro`, `_Pragma` in user code;
unmodelled compile-command options (§4.1).

Accepted with pruning disabled (`flat.cpp` at best): an unknown `#if` left in
user code after resolution; an include of a system header outside the
standard-header list; libraries other than `-lm`, `-lpthread`, `-lstdc++`
(external code can call into user code, R4-K).

### 1.3 Trust assumptions (user, 2026-11-15)

Some judge facts can't be proved from the user's machine. The design relies on
these, and only these; the report lists each one an output depended on, and
§4.6 checks them wherever a compiler for the configuration is available.

- **A1** The judge's standard library headers (the C and C++ standard headers
  and the libstdc++ extension headers in the standard-header list, §4.5)
  define only reserved names, names the C or C++ standard specifies, and the
  names in `distill`'s extension list for that library (POSIX/GNU names such
  as `M_PI`, built from the libraries' headers, §4.5).
- **A2** Those headers never `#undef` or redefine a non-reserved macro the
  user's code defines, unless the name is in the lists of A1. Also (user,
  2026-11-15): (a) they never `#undef` or redefine a macro the compiler
  predefines (`__GNUC__`, `_WIN32`, …); (b) an expansion of one of their
  macros refers to a user-declared name only if that name appears verbatim as
  an identifier token in the invocation's arguments (no names synthesized with
  `##`, R5-3).
- **A3** The judge doesn't predefine (on its command line or in the compiler)
  a macro that the user's code itself `#define`s, such as an include guard.
- **A4** A judge profile's facts are accurate as of its `updated` date: its
  command, and for a *reproduced build* (§4.3) that the judge's compiler is the
  named package or was built from the published recipe, without further patches,
  specs or startup headers, so the reproduced compiler's preprocessing
  environment is the judge's.
- **A5** (user, 2026-11-15) The judge's compiler and command add no
  non-reserved macro beyond the profile's macro operations.
- **A6** (user, 2026-11-16) The judge's standard library headers call a
  user-declared function, function template or hidden friend (unqualified,
  through argument-dependent lookup, or through a customization point object)
  only under a name in `distill`'s ADL list, or as an operator function. The
  list is the names the C++ standard specifies as found by argument-dependent
  lookup from library code or as customization points (`swap`, `iter_swap`,
  `iter_move`, `begin`, `end`, `cbegin`, `cend`, `rbegin`, `rend`, `crbegin`,
  `crend`, `size`, `ssize`, `data`, `cdata`, `empty`, `get`, …), kept in
  `data/names/adl.txt` (D3-1-2). The registry is versioned per supported
  dialect and records for each name its lookup mechanism and normative clause
  (D3-2-2). It covers library and language-generated lookup, including
  unevaluated probes and constraints: at least swapping and iterator
  customization (`swap`, `iter_move`, `iter_swap`), range access and range-for
  (`begin`, `end`, `rbegin`, `rend`, `size`), structured bindings (`get`),
  error-code conversion (`make_error_code`, `make_error_condition`),
  comparison customization (`strong_order`, `weak_order`, `partial_order`),
  chrono parsing (`from_stream`) and the valarray mathematical functions
  (`abs`, `acos`, … `tanh`). It distinguishes public CPO names from the names
  they look up; names kept only because a CPO of that name exists are harmless
  over-retention. Member customization, callbacks (a function passed as a
  value) and permitted template specializations (`hash`, `formatter`,
  `tuple_size`, `tuple_element`, iterator traits, range variable templates)
  are not in the registry; separate rules cover them (§4.7). A6 covers the
  standard headers only, not GNU extension headers (§4.7 preconditions); an
  unsupported dialect disables pruning.

**Reserved** means what C++ reserves: identifiers containing `__` anywhere,
or beginning with `_` and a capital letter (R4-A).

## 2. Usage

```
distill [distill options] [<compile command>]
```

The compile command is what the user would type to build the solution:

```
distill -o submit.cpp g++ -std=gnu++23 -O2 -I ~/lib main.cpp
distill --judge codeforces -o submit.cpp g++ -I ~/lib main.cpp
distill main.cpp                      # same as: distill g++ main.cpp
```

Distill options (before the compiler name):

| Option | Meaning |
|---|---|
| `-o <file>` | output (default stdout) |
| `--judge <name\|file>` | judge profile (§4.3) |
| `--keep <name>` | extra root; in source: `// distill: keep` before a declaration |
| `--verify-run <path>` | also compare runs on a sample input (a file, or a directory of `*.in`, sorted, no recursion); repeatable |
| `--report <file>` | the report (§4.9) |
| `--no-prune` | inline and resolve only |
| `--strip-comments` | remove all comments |
| `--strict` | exit 3 unless the written file is `pruned` with verification `target` |
| `-v` | progress on stderr |

Without a compiler name the compiler is `g++` from `PATH`. If the probe finds
Clang (the stock macOS `g++`), `distill` stops and asks for a real GCC (e.g.
`g++-16`) or a profile with a cross compiler. Clang named explicitly is
accepted and noted.

**Result.** The written artifact is the most transformed one that passed its
checks (§4.6): `pruned`, else `flat` (resolved, not pruned), else `raw`
(inlined only). Its verification level is `target` (checked with a compiler
whose preprocessing environment is the judge's, §4.6) or `local` (checked with a
compiler that matches only the facts the transformation used). Runtime
comparison has its own status: `compared N samples`, `skipped: can't run
target binaries here`, or `not requested`.

| Exit | Meaning |
|---|---|
| 0 | a file was written |
| 1 | nothing written: invalid or unsupported input, the original doesn't build, or not even `raw` passed its checks |
| 3 | `--strict`, a file was written, and it is not `pruned`/`target` |

The result is one line on stderr and the header of the report.

## 3. Pipeline

```
compile command ─► 1 command model ─► 2 probe ─► 3 baseline (build original, run samples)
main.cpp + headers ─► 4 inline (text) ─► raw.cpp ─► 5 resolve #if ─► flat.cpp
                                       6 parse + prune ─► candidate
                                       7 validate each artifact in a configuration that satisfies its facts (§4.6)
                                       8 write the most transformed artifact that passed; report
```

Every step runs under limits: include depth (200), total inlined size
(64 MiB), macro-expansion steps, wall clock per step and per run.
Captured process output is capped at 4 MiB per stream, except the `-E`
output of preprocess checks, capped at 64 MiB (D3d). Clang
parses, compilers and sample programs run in child processes, in a process
group (POSIX) or Job Object (Windows) killed as a whole on timeout, with pipes
drained concurrently, output capped and children reaped. A step that hits a
limit fails its artifact; a deadline never promotes an unchecked artifact.

## 4. Components

### 4.1 Command model

The compile command is parsed into a structured invocation by exact option
spelling, never by prefix (R3-8). Response files (`@file`) are expanded
first.

| Class | Options | Treatment |
|---|---|---|
| input | exactly one `.cpp`/`.cc`/`.cxx` | the source |
| modelled | `-I`, `-iquote`, `-isystem`, `-D`, `-U`, `-std=`, `-m32`, `-m64`, `-funsigned-char`, `-fsigned-char`, `-fshort-wchar`, `-fno-exceptions`, `-fno-rtti`, `-march=`, `-mtune=` | translated into the Clang parse and the configuration object (§4.6) |
| harmless | `-O0`…`-O3`, `-Os`, `-Og`, `-Ofast`, `-g`, `-g0`…`-g3`, `-W<name>` and `-Wno-<name>` for warning names (not `-Wp,` `-Wl,` `-Wa,`), `-w`, `-pedantic`, `-pedantic-errors`, `-pthread`, `-static`, `-static-libstdc++`, `-static-libgcc`, `-s`, `-lm`, `-lpthread`, `-lstdc++`, `-fdiagnostics-color=…`, `-fmax-errors=…`, `-pipe`, `-fsanitize=…` | passed to every build unchanged |
| libraries | other `-l<lib>`, `-L<dir>` | passed to builds; pruning disabled (R4-K) |
| controlled | `-o`, `-MD`, `-MMD`, `-MF`, `-MT` | `-o` replaced per build by a temporary path; dependency outputs dropped |
| rejected | `-c`, `-S`, `-E`, `-include`, `-imacros`, `-Wp,…`, `-Xpreprocessor`, `-Wl,…`, `-Xlinker`, `-Wa,…`, `-Xassembler`, `-save-temps`, `-B…`, `--sysroot`, `-isysroot`, `-M`, `-MM`, more than one input | error |
| unmodelled | anything else | passed to builds; disables resolution and pruning (only `raw.cpp`), with a message naming the option |

`-D`/`-U` in the compile command are local settings (e.g. `-DLOCAL`): they
apply to the baseline and the `raw` checks only; configurations that check
`flat.cpp` and the candidate replace them with the profile's macro operations (§4.6).

Processes are started with `posix_spawn` on POSIX and `CreateProcessW` with
explicit MSVC-runtime argument quoting on Windows, never through a shell. The
original is built from its own directory.

### 4.2 Probe

For each compiler `distill` uses, run with an empty temporary source file:
the `"…"` and `<…>` search lists (`-E -v`), the target triple
(`-dumpmachine`, cross-checked with `-m32`/`-m64`), predefined macros
(`-dM -E`, with the configuration's defines), compiler family and version from
those macros, and the dialect from `-std=` (or the compiler's default from
`__cplusplus` and `__STRICT_ANSI__`).

The Clang parse uses `-nostdinc++ -nostdlibinc`, the probed search lists in
order minus GCC's own compiler-internal include directory (Clang's embedded
resource headers replace it, placed so `include_next` from libstdc++ reaches
them), the probed triple and dialect, the modelled flags. Clang keeps its own
predefined macros. A parse error in a system header disables pruning.
Which compiler is probed for the parse: §4.7 "Parse configuration".

A **cross compiler** for the judge's target (e.g. `x86_64-w64-mingw32-g++` for
Codeforces, `brew install mingw-w64`) can give verification `target` for builds (§4.6);
the Clang parse then uses the judge's triple.

### 4.3 Judge profiles

A data file (TOML), shipped for **Codeforces** and **AtCoder** first; user
profiles in `~/.config/distill/judges/<name>.toml` (`%APPDATA%\distill\judges`
on Windows):

```toml
name = "codeforces"
description = "Codeforces, GNU G++23 14.2 (64 bit, msys2)"
triple = "x86_64-w64-mingw32"
compiler = { family = "gcc", version = "14.2.0" }
std = "c++23"                      # the language the user selects on the judge
flags = ["-O2", "-DONLINE_JUDGE"]  # compile options in order, -D/-U in place
link = ["-static", "-Wl,--stack=268435456", "-lstdc++exp"]
# Judge package revision unknown; this compiler produces an approximate snapshot.
snapshot_compiler = { package = "msys2:mingw-w64-x86_64-gcc=14.2.0-3" }
libraries = ["mingw-w64"]          # selects the A1 extension lists
predefined = "codeforces-predefined.txt"
check = ["x86_64-w64-mingw32-g++"] # compilers for the judge's target, tried in order
updated = "2026-11-15"
sources = ["https://codeforces.com/blog/entry/133046"]
```

Researched from the judge's published information when written, dated, and
updated by PR. Without a profile the dialect comes from the compile command
and nothing else about the judge is known.

**Command** (P1-5, P1-6, P2-6). `flags` is parsed structurally by the §4.1
command model with the same rejected list (no `-include`, `-imacros`, `-Wp,…`,
`-specs=`, `-B…`, …); an unmodelled option is a profile error unless the
profile lists it in `inert` (options it documents as not affecting
preprocessing, such as `-fconstexpr-depth=…`). `-D`/`-U` stay an ordered list
of macro operations with exact replacement text (`-DX` is `1`, `-DX=` is
empty). The profile's `std` is the dialect: a `-std=` in `flags` is a profile
error, and `std` overrides `-std=` in the user's compile command (warning if
they differ). `link` holds linker-only options and never affects macros.

**Build provenance** (P1-4, P2-1, P2-2). `build` names the judge's compiler:
an exact distribution package or the judge's published build recipe (source
release and configure line). The snapshot is produced in CI by that package or
by a compiler built there from that recipe: a **reproduced build**. A profile
whose snapshot comes from another compiler of the same version and target is
**approximate**. When the judge build is unknown, omit `build`; the optional
`snapshot_compiler` names the actual snapshot compiler using the same exact
`package` or published `recipe` form, or a snapshot-only
`image = "docker.io/<namespace>/<repository>@sha256:<64 lowercase hex>"` form.
Image references require a digest and forbid tags; `build` does not accept
`image`. A snapshot-only `homebrew` value matches
`homebrew:gcc=[1-9][0-9]*\.[0-9]+\.[0-9]+@sha256:[0-9a-f]{64}` in full:
a core GCC version and exact bottle blob digest. It is also forbidden in `build`
and records `reproduced: no`. The image pin identifies snapshot acquisition without asserting that
the judge grades in that image. It supplies provenance, not reproduction:
the snapshot records `snapshot-compiler` and `reproduced: no`. A snapshot claiming
reproduction without `build` is rejected. Codeforces publishes only
"GNU G++23 14.2 (64 bit, msys2)", with no package revision, so its profile uses
an available MSYS2 package as `snapshot_compiler` and remains approximate.

**Snapshot** (P2-3): the macro state at the first token of an empty source
under `-x c++ -std=<std> <flags>` (macro operations included, in order),
recorded per name with its provenance: compiler builtin, startup header
(`stdc-predef.h`, detected from the line markers of a separate `-E` run, since
`-H` starts tracing after it), or command operation. Any other header
processed before the source is a profile error.

**Special builtins** (P1-1, P1-2, P2-5). An embedded registry per compiler
family and version, taken from the compiler's source (`libcpp/init.cc` for
GCC): `__LINE__`, `__FILE__`, `__COUNTER__`, `__has_include`, `__has_embed`,
`__has_extension`, …. The snapshot step probes each with `#ifdef` under the
profile's command. A name's state follows the probe and the effective macro
operations (`-U__has_include` makes it undefined; `-D__has_include(x)=0` makes
it an ordinary known macro); a name the registry lists and the probe confirms,
without an ordinary definition, is *defined, definition unknown*. A condition
is unknown when it invokes an active special builtin (directly or through
expansion), not when it only tests `defined(name)`. A compiler version without
a complete registry entry makes the profile approximate.

**Host-dependent options** (`-march=native`, `-mtune=native`, `-mcpu=native`;
P1-3, P2-4). Their effect is modelled by an audited, versioned **native
dependency model**: every macro the compiler can derive from the CPU it runs
on, from its target code (`gcc/config/i386/i386-c.cc` for x86: ISA feature
macros, `__tune_*`, CPU-name macros) and its common code
(`c-family/c-cppbuiltin.cc`: `__FP_FAST_FMA*`, `__GCC_DESTRUCTIVE_SIZE`,
`__GCC_CONSTRUCTIVE_SIZE`, …). The configurations a native option allows are
the CPUID feature masks and cache parameters the driver can detect, not the
named `-march=` values: the driver enables ISA options no named CPU carries
(`-mshstk`, `-mrtm`, …) and passes measured cache sizes. Every model name is
therefore *unknown* under a native option, unless a command macro operation
(`-D`/`-U`, applied after the target's builtins) fixes it on every CPU; those
names take the joined state (absent everywhere *undefined*, one definition
known, differing definitions *defined, definition unknown*, otherwise
*unknown*). `-mtune=native` alone varies only tuning-derived macros. Names the
source audit shows depend only on ABI options or nothing, listed in the model
file header, are outside the model and keep their snapshot state. The x86-64
SSE baseline stays in the model because CPUID bits can be masked
(2026-11-17, user). The regression sweep over the compiler's `-march=` values
plus the native run already fails a snapshot if a name outside the model
varies; it never supplies states (fixed 2026-11-17 after the AtCoder
probe found `__SHSTK__` defined on a runner while the sweep-based snapshot
recorded it as undefined).

**Facts** a profile supplies, each with its provenance recorded:

- reproduced build: every name as the snapshot, the special-builtin probes and
  the native dependency model give it;
- approximate profile: only allow-listed facts (compiler identity, target OS
  and architecture, dialect, type sizes and limits, character signedness) are
  taken from its snapshot; every other reserved name is *unknown* (P2-1);
- every profile: an unlisted non-reserved name is *undefined* (A5); an
  unlisted reserved name is *undefined* for a reproduced build (A4) and
  *unknown* for an approximate one.

### 4.3.1 Profile names, aliases and the judge probe

Added in v9 (2026-11-16, user), after Codex round J1
(`docs/reviews/2026-11-16-codex-judges-r1.md`, cited as *J1-n*).

**Names** (J1-14). One profile per judge compiler option, named
`<judge>-<family><major>-<std>`: `codeforces-gcc14-cpp23`,
`atcoder-gcc15-gnupp23` (charset `[a-z0-9-]`; `c++23` is spelled `cpp23`,
`gnu++23` `gnupp23`). A further `-<variant>` suffix (`-32bit`, `-msvcrt`, …)
appears only when two options of one judge would otherwise collide. Full names
never change meaning; a judge change that alters a profile's facts beyond a
re-research of the same option gets a new name. Clang and MSVC options are out
of scope (the checks need GCC).

**Aliases** (J1-13). Optional `alias = "<judge>"`, at most one profile per
alias, pointing at the judge's newest GCC option (user: "most recent compiler,
most features"). Aliases move between releases; full names are the way to pin.
Every run prints `judge: <selector> -> <resolved name>` on stderr, the report
records it, and release notes list alias moves. Resolution is deterministic:
a user profile whose file name or alias equals a bundled identifier shadows
the bundled one, with a warning on every run; two user profiles claiming one
alias, or a name equal to another profile's alias, is an error. The report
records selector, resolved name, origin (bundled, or user with path) and the
profile content digest (J1-15). `--judge list` prints names, aliases,
descriptions, `updated`, reproduced/approximate and the latest stored
observation. The shipped `codeforces` and `atcoder` profiles are renamed with
those aliases, so `--judge codeforces` keeps working.

**Judge probe** (J1-1…J1-12, J1-16). A diagnostic, never a source of facts.

1. `distill --judge-probe <profile>` prints one self-contained C++ source,
   deterministic per profile digest and probe-format version, with that
   version, the profile name and digest in its output header. The distill
   version is not part of it, so expected output from a CI build stays valid
   for released binaries; any change to the generated facts or encoding bumps
   the probe-format version, and a unit test checks every shipped expected
   output against the current generator.
2. Run in the judge's custom test (Codeforces "Custom Invocation", AtCoder
   "コードテスト") under the profile's option, it prints one line per checked
   fact and a trailer with the line count and a checksum, which detects
   truncation and accidental edits (not fabrication):
   - presence (`#ifdef`) of every snapshot name, every registry special
     builtin, and a fixed list of judge-style names the profile treats as
     undefined (`ONLINE_JUDGE` when not set, `EVAL`, `LOCAL`, `_DEBUG`, …);
     presence is captured before the probe defines anything;
   - then, with helpers under reserved generated names
     (`__distill_probe_…`), the stringized expansion of each object-like
     snapshot macro in an audited subset: its replacement, transitively per
     the snapshot, contains no contextual builtin (`__LINE__`, `__COUNTER__`,
     `__FILE__`, `__BASE_FILE__`, `__INCLUDE_LEVEL__`, `__DATE__`, `__TIME__`,
     `__TIMESTAMP__`), no `_Pragma` and only balanced parentheses (J1-5…J1-7).
     Function-like macros and all others are presence-only and reported so;
   - then `__has_include` for the headers the A1 lists rely on and library
     version macros, each read after the library header that defines it; these
     lines are diagnostics and never validate A1 inventories (J1-9).
3. The expected output is produced in CI by compiling and running the probe
   with the snapshot compiler under the profile's command, in the job that
   produces the snapshot, and ships as `<name>-probe-expected.txt`, so both
   sides stringize with a real GCC.
4. Native-dependent names (`-march=native`) are compared against the states
   the native dependency model permits; names it holds invariant are compared
   normally; samples never narrow the model (J1-8).
5. The probe has source and output budgets per profile (from the judge's
   published limits). Above them, `--part N/M` emits parts whose output
   carries a manifest (part id, fact-set hash, profile digest); the check
   rejects missing, duplicate or mixed parts (J1-11).
6. `distill --judge-check <profile> <output-file>…` validates header,
   trailer and manifest, then reports per fact: match, mismatch (expected vs
   judge), missing, extra, unsupported, with checked/skipped counts. Failures
   are classified: format error, wrong compiler or option selected,
   contextual field, probe failure, fact mismatch, contradicted assumption (a
   name the profile treats as undefined was found defined). Exit status 0 only
   when nothing failed. `--record <dir>` writes the observation as
   `<name>-<YYYY-MM-DD>-<n>.txt` for a PR to `data/judges/observations/`.

**Meaning of an observation** (J1-1…J1-4, J1-10, J1-12). It is evidence,
scoped to "custom test of that option, that date", and is labelled
*user-reported match*, never *verified*. It never changes the facts of §4.3:
equal stringized expansions do not imply equal definitions (`X` = `Y`, `Y` = 1
versus `X` = 1), presence says nothing about a function-like macro's
definition or a builtin's activity, pasted output is not authenticated, and a
custom test need not equal the grading configuration. A passing check claims
nothing about unlisted names: A4 and A5 remain assumptions. A fact mismatch or
contradicted assumption on a shipped profile blocks the next release until it
is investigated; there is no automatic downgrade.

### 4.4 Inliner (text level)

Textual substitution, not preprocessing:

- Each `#include` naming a user header is replaced by the header's text, in
  place, recursively, also inside conditional blocks. Lookup follows the
  compiler's rules separately for `"…"` (including file's directory,
  `-iquote`, `-I`, system) and `<…>` (`-I`, system). A header found in a user
  path is a user header; one found in a system path stays an `#include`; an
  angle include not found locally (e.g. `<windows.h>` on Linux) stays; a
  quoted one not found locally is rejected (§1.2). A quoted include that
  resolves to a system header is rewritten to `<…>`.
- Everything else stays where it is. No hoisting, no deduplication here.
- `#pragma once` is converted to a guard only as the first directive of a
  file, outside any conditional: `DISTILL_ONCE_<hash of file identity>`
  (device + inode, or volume + file index on Windows), checked against all
  identifiers in the input. Any other `#pragma once` is rejected.
  Generated guard directives have no original D2 location. Their presumed
  coordinates use the guarded original file, followed by a marker restoring
  its first source line; no marker may name an internal raw/flat/pruned file.
  Raw retains the complete guard. When resolution proves its condition true,
  flat removes the `#ifndef`/`#endif` and keeps the harmless `#define` so later
  inclusions still resolve as dead. Pruned retains that define too.
- **Recursive inclusion** (a header reached again while on the include stack)
  is dropped only if the header's whole content is one guard `G`, `#define G`
  textually precedes the recursive include, no `#undef G` appears in user
  text, user code contains no `push_macro`/`pop_macro`/`_Pragma`, `G` is a
  non-reserved name that A2 protects, and between them there is no system
  include outside the standard-header list. Otherwise rejected (R4-F). Repeated non-recursive inclusions are inlined again; the resolver
  removes the dead ones.
- **Line numbers are always preserved** (R4-G): `#line` markers are emitted
  wherever a step changes line numbering (start and end of every splice,
  every resolved directive, every deletion), and the renderer (§4.8)
  maintains them to the final bytes. Every line of the output has the logical
  line it had in its original file, so `__LINE__`, `__builtin_LINE` and
  default arguments that capture the call site behave as before. Cost: one
  `#line` per change point.

`raw.cpp` behaves under any judge's preprocessor like the original files
would with the user headers present.

### 4.5 `#if` resolution

Removes branches of `raw.cpp` that are dead on the judge, given the facts and
A1–A3. Result: `flat.cpp`. Disabled by `__COUNTER__`, macro-stack pragmas,
`_Pragma` and unmodelled options (§1.2).

**State.** At each program point every macro is *undefined*, *defined with a
known definition* (object- or function-like, full replacement list),
*defined, definition unknown*, or *unknown*.

**Initial state** (R3-1, R3-3, R4-E). These are the *external
requirements* of a resolution (§4.6):

- the dialect (`__cplusplus`), the target's preprocessor arithmetic and
  character signedness;
- with a profile: the facts of §4.3 (reproduced build or approximate), except
  as below;
- without a profile: every reserved name *unknown*;
- a non-reserved name that the user's code `#define`s anywhere: *undefined*
  until its first definition on the path (A3). A profile that predefines or
  `defines` such a name is a conflict with A3: resolution is disabled with an
  error naming it;
- any other non-reserved name: *undefined* with a profile (A5), *unknown*
  otherwise.

States reached later from `#define`/`#undef` in the source are *derived*;
they are not compared with any compiler probe (R4-C).

So include guards and library switches the user defines resolve with or
without a profile; `#ifdef LOCAL` (tested, never defined) resolves only with a
profile.

**Transitions:**

- `#define`/`#undef` in an active region set the state.
- An include of a header on the **standard-header list** (C and C++ standard
  headers, `<bits/stdc++.h>`, `<ext/pb_ds/…>`, `<ext/rope>`): every name in
  the *may-define set* becomes *unknown*; user macros and compiler-predefined
  macros keep their state (A2 (a), R4-A). The may-define set = every
  reserved name that is not compiler-predefined ∪ the macro names the C and
  C++ standards specify (one embedded list, the same one A1 refers to) ∪ the
  extension list for the library (with a profile, its `library`; without, the
  union of all lists). An include never makes a macro *defined*, only
  *unknown* (R3-1).
- An include of any other system header: every macro becomes *unknown*,
  including user macros (R3-2).
- `#pragma GCC target`, `optimize`, `push_options`, `pop_options`: feature
  macros (`__AVX2__`, `__OPTIMIZE__`, …) become *unknown*.
- **Unknown condition** (below): the block is kept with all its branches and
  directives. Each alternative (`#if`, each `#elif`, `#else`) is walked from
  the block's entry state, never from a preceding alternative's end state; a
  block without `#else` has an implicit empty alternative. Afterwards each
  macro's state is the join over all alternatives' end states, equal states
  staying known (R4-D). An `#elif` after an unknown condition is itself
  unknown. The block keeps every original directive and span; conditions
  nested inside its alternatives are still resolved, and `#line` markers are
  added where those removals need them (D2d).

**Extension lists** (A1) are data files in the repo, one per library
(`glibc`, `libstdc++`, `mingw-w64`, `macos-libc`), generated by a script that
collects non-reserved `#define` names from those libraries' headers across the
versions in the compatibility matrix, and reviewed by PR. They overapproximate:
a name in the list only costs a resolution.

**Evaluating a condition** (R3 "expansion model"):

1. Compute the macro closure of the condition: identifiers in the condition,
   plus identifiers in the replacement lists of every *known* macro reached,
   transitively, including function-like macro bodies.
2. If the closure contains a macro whose state is *unknown*, or
   *defined, definition unknown* outside a `defined(…)` operand, or any
   replacement list in the closure uses `##` (pasting can create names outside
   the closure), or `__has_include`, `__has_builtin`, `__has_attribute`,
   `__has_cpp_attribute`, `__has_feature` appears: the condition is
   **unknown**.
3. Otherwise the value is computed by Clang's own preprocessor on a
   synthetic input in an **isolated environment** (R4-D): `-undef` and no
   predefined macros of Clang's own, the target triple's integer widths and
   character signedness (from the external requirements; unknown if they
   aren't known), then exactly the known definitions of the closure, then the
   `#if` with its condition. Names known *undefined* are absent; a name
   *defined, definition unknown* that appears only as a `defined` operand is
   given a placeholder definition; parameters of function-like macros are
   bound names, not dependencies; replacement lists of names used only under
   `defined` are not expanded. A preprocessor error makes the condition
   unknown.

Missing external requirements (D2d): without a known integer layout (no
profile), a condition resolves only if its whole closure stays in the
target-independent Boolean subset (identifiers, `defined`, `0`, `1`,
parentheses, `!`, `&&`, `||`); any other arithmetic, character literal or
alternative token is unknown. Without a known dialect, literals and
dialect-dependent tokens are unknown too. The compile command's dialect fixes
`__cplusplus` even without a profile. When no complete native dependency
model exists for the compiler version, the feature-macro pragmas above make
every reserved name unknown. The A3 conflict check uses the profile's
effective snapshot and command definedness even where an approximate profile
withholds the value from the resolver.

This makes the resolver's own logic small: the closure walk and the state
machine. Expansion, rescanning, variadics and arithmetic are Clang's.

**Known condition:** the dead branches and the `#if`/`#elif`/`#else`/`#endif`
lines are removed; the live branch stays. Include guards are ordinary
conditions: the first inclusion resolves true, later inclusions resolve false
and disappear, which is the only deduplication.

The report counts conditions resolved and kept per reason, so the effect on
the user's library is measured.

### 4.6 Validation configurations

A **configuration** is an object: compiler (path, family, version, triple),
dialect, ordered macro operations, include paths, options, recorded as the
exact argument vector, including any profile option it dropped or replaced
(P2-6). A configuration **satisfies** a resolution if rerunning the resolver with
the configuration's own external requirements (its probe in exactly that form:
`-dM -E`, the special-builtin probes, dialect, arithmetic,
character signedness) in place of the judge's makes the same decision for
every condition the resolution decided (R4-C), compared directionally (D2d):
the rerun must reproduce every true/false decision, or show the directive
unreachable in that configuration; unknown and dead entries impose nothing;
the directive inventory and locations must agree. Derived source states are
recomputed by that rerun, not compared. Builds of
`raw.cpp`, `flat.cpp` and the candidate run in a temporary directory without
the user's `-I`/`-iquote` paths, so a missed inline is a build error.

| Artifact | Configuration | Checks |
|---|---|---|
| baseline | local (compile command as given) | builds; runs samples |
| `raw` | local | preprocess check against the original; builds; runs equal the baseline |
| `flat` | the first configuration that satisfies the resolution: a `check` compiler from the profile, else the local compiler with the profile's macro operations replacing local `-D`/`-U` | preprocess check `raw` vs `flat` in that configuration; builds; runs equal the original built in the same configuration |
| `pruned` | same as `flat` | builds; static name invariant (§4.7); rendering invariant (§4.8); runs equal `flat`'s |

The **preprocess check** compares the two files' `-E` output in that
configuration *with* line markers: the sequence of tokens together with each
token's logical line, with file names mapped to the original files'
identities, and directive boundaries (`#pragma` lines) kept (R4-B). It is one
check, not a proof: it catches a resolver or inliner decision that differs
from what that configuration's preprocessor does, including branches that
differ only in their line, and violations of A1–A3 by that configuration's
headers. It can't detect a difference that the configuration doesn't
exhibit.

Verification level of the written artifact: `target` if its configuration
has the judge's preprocessing environment: the profile is a reproduced build,
the configuration's compiler probes identically to the snapshot (macros and
special builtins) under the profile's full `flags`, and the command has no
host-dependent option (a local `-march=native` describes the local host,
P2-6); otherwise `local`. If no configuration satisfies the facts a
`flat` resolution used, `flat` and `pruned` are not candidates and `raw` is
written, with the reason in the report. A build in a non-satisfying
configuration may be run as a diagnostic and is reported as such; it never
certifies anything.

Runtime checks need the binaries to run here; with a cross compiler for
another OS they are skipped and the status says so. A runtime difference
rejects that artifact and the next less transformed one is used; it is
reported as "differs on sample X (distill bug or nondeterministic program)",
never as a proven bug. If `raw` differs from the baseline, nothing is written
(exit 1) and the files are kept for a bug report.

No repair loop in the first version: a failed candidate falls back to `flat`.

Clarifications from D2e (implementation, reviewed):

- **No profile.** Resolution uses source-derived facts, A3 and the command's
  effective dialect (including the compiler's probed default); it never
  acquires the local compiler's predefined macros or integer model. Only the
  satisfaction rerun uses the local configuration's observed facts.
- **Observed configurations** become resolver states by the same rules as
  snapshots (§4.3 "Facts", §4.5): a complete probe of a GCC version in the
  special-builtin registry is exhaustive *for that configuration*, which is
  not judge build provenance; unregistered versions use the approximate
  allow-list. A3 is checked against its effective definitions; A5's local
  counterpart is absence from its macro dump and special probes.
- **`flat` options.** Keep the other local compile options; remove local
  macro operations, dialect and user include paths; append the profile's
  `-std` and full `flags` in order. A `check` compiler gets the profile's
  `link`; the local fallback keeps local link options and reports the profile
  link options it omitted. Every probe, preprocess and build argument vector
  and cwd is recorded.
- **Check compilers** whose probed triple isn't compatible with the profile's
  are skipped and reported.
- **First satisfying configuration is authoritative:** if `flat`'s
  preprocess check or build fails there, `raw` is written; no further
  compiler is searched.
- **`target`** requires exact equality of ordinary macros and of every
  registered special probe, compatible identity, triple and dialect, the
  profile's full `flags` in order in the configuration, and no host-dependent
  option in the command or the configuration.
- **File identities** in the preprocess check: user files compare by
  filesystem identity (shown by one original spelling); system headers
  (marker flag 3) by spelled path.
- **Limits:** each step's deadline covers all of its child processes and
  evaluations, including every candidate configuration; production Clang
  evaluations run in a bounded worker process. Process output is capped at
  4 MiB per stream by default; each input file is read with a 64 MiB ceiling.
- **Work files** live in a private directory under the system temporary
  directory, never next to the sources; they are deleted unless `raw` fails.
- **Assumptions** reported are rule dependencies, conservatively, not a
  minimal set: `raw` reports the once-guard/recursion ones; `flat` adds the
  provenance of the external facts used (A3/A4/A5) and A1/A2 when standard
  headers take part; `target` adds A4.
- **`--strict`** exits 3 for every result until D3, since none is `pruned`.

### 4.7 Pruning

**Input:** the checked `flat` bytes and their D2 provenance: the line map,
which `#line` markers are generated, the directive inventory, the resolution
record and its disabling reasons, and the `// distill: keep` annotations
with the declarations they precede, extracted before any comment stripping
(D3-2-5). `unknownInUserCode` is authoritative (generated
`#pragma once` guards count as user code); a Clang parse that happens to
take one branch never clears it.

**Preconditions.** Pruning runs **only if no unknown conditional remains in
user code** (R4-H, R4-I, R4-J) **and no predefined macro is used as a value**
(R5-1, D3-1-3): if any macro expansion whose expansion location is in user
code outside `#if`/`#elif` conditions expands a macro that Clang's parse
environment or the judge configuration predefines, and the two definitions in
effect at that point disagree, or either is not known, pruning is disabled.
Expansions are recorded with Clang's preprocessor callbacks, so names
synthesized by `##` and found on rescanning (`CAT(__GNUC, __)`) count like
spelled ones; the judge's definition in effect is the resolver's state at
that point (so a user `#undef __clang__` is followed), from the snapshot or
the facts.

**Token pasting** (D3-2-1). A2 (b) doesn't make expanded user tokens equal:
a user macro can paste a system macro's value (`JOIN(pick, BUFSIZ)` gives
`pick8192` with glibc and `pick512` with MinGW). So a `##` in a user macro
expansion reached in the parse disables pruning if any of its operand tokens,
after argument expansion, comes from the expansion of a system-header macro
or of a predefined macro. Operands made only of tokens spelled in user code
and expanded from user macros are the same on the judge (no unknown
conditional remains in user code, and A2 protects user macros).

Without a profile, any compiler, platform or judge conditional
(`#ifdef __GNUC__`, `#ifndef ONLINE_JUDGE`) therefore leaves the result at
`flat`; with a profile snapshot such conditions resolve. Also disabled by an
include of a system header outside the standard-header list, by an include
of a GNU extension header (`ext/…`, `bits/extc++.h`) whose lookup surface
hasn't been audited (D3-2-3: `<ext/rope>` calls `copy_n` unqualified); D3
audits `ext/pb_ds/` and lists it as covered or not, by
non-standard libraries (§1.2), and by a parse error, an unsupported
construct or a limit anywhere: an error-recovery AST is never analysed.

**What the preconditions give** (D3-1-4, D3-2-1). With them, the pasting
rule and A2 (b), the user code Clang analyses expands to the same identifiers
as the code the judge compiles; the values that system macros supply (such
as `BUFSIZ`) may differ. This is a statement about preprocessing only. It does not make the
library's declarations, layouts, overload resolution or template selection in
Clang's parse the judge's (the parse may use another target, §4.2, and other
headers). So the keep rules never treat a missing AST edge *from system code
into user code* as evidence that a user declaration is unused: such calls
are covered by A6 and the never-removed ADL names, and user specializations
of system templates by rule 3 and the roots. AST edges from system code are
positive evidence only.

**Parse configuration** (D3-1-1). Clang parses in a configuration derived
from the selected `flat` checking configuration (§4.6): its probed triple,
search lists (as §4.2) and dialect, its effective macro operations (the
profile's replacing local `-D`/`-U`, in order, after Clang's predefinitions),
and its semantic options translated to Clang's. Options without a Clang
translation are listed in the report; one that isn't on the
semantics-inert list disables pruning. The report names the parse
configuration separately from the checking configuration. A `target` level
describes the checking configuration; it is not a claim that Clang's
semantics are the judge's.

**Data model:** *entities* (Clang declarations, user and system),
*retention families* (indivisible sets of user entities), *edit ranges*
(disjoint byte ranges in the immutable `flat.cpp` buffer, one or more per
family). Overlapping retention obligations are merged with union-find over
families; source ranges are never merged. Original and presumed (`#line`)
locations are for provenance and the report only, never for editing
(several physical copies can share an original location).

**Families and ownership** (D3-1-6). Every user token belongs to exactly one
family or to a structural shell; an unrecognized declaration is pinned with
its dependencies.

- a top-level declaration group (`int a, b;`, `struct S {…} s;`);
- a record with all its lexical contents: members, friends (including hidden
  friend definitions), nested declarations, and its out-of-line member
  definitions; a non-inert static data member definition is a root (rule 1)
  and so keeps its record;
- an overload set: all non-member functions and function templates with the
  same name in the same namespace, with their redeclarations;
- a template family: primary, partial and explicit specializations,
  deduction guides; a user specialization of a system template (`std::hash`)
  is its own family;
- an enum; a typedef or alias declaration; a namespace alias; a
  using-declaration or using-directive; a concept; a variable with its
  redeclarations (a structured binding declaration is one variable family);
- roots by kind: a namespace-scope `static_assert`, an explicit instantiation
  declaration or definition.

Local declarations belong to their enclosing family. Namespaces and linkage
specifications (`extern "C" { … }`) are *shells*: they don't root their
contents; an emptied shell goes only if its name no longer appears.

**Directives.** After resolution only conditionals in system headers remain,
so user code has none (pruning precondition). Source directives in user code
that remain (`#define`, `#undef`, `#pragma`, system `#include`) are never
removed; a family whose range crosses one is pinned (a function containing a
`#define` stays whole). Generated `#line` markers (D2 provenance, not their
spelling) are rendering metadata, not directives: they don't pin, and §4.8
recomputes them (D3-1-8).

Never removed in the first version: macro definitions, the members of a kept
record (a record goes only as a whole), declarations produced by a macro
invocation (the whole invocation and everything it declares is pinned), **every user
non-member operator family** (R3-4), every namespace-scope
`using`-directive and `using`-declaration (lookup depends on them without
naming them, R4-I), every `extern "C"` function or variable definition
(the C library and other external code can call or reference them by name,
R4-K), every user specialization of a system template whose template
arguments name no user type, and **every user function, function template or
hidden friend whose name is in the ADL list** (A6, D3-1-2), whatever its
parameters.

**Indexed names** (for the name rule): the names a family declares at
namespace scope: record, enum, typedef/alias, function, variable, template,
concept and namespace-alias names, and unscoped enumerators. Member names,
template parameter names and local names are not indexed.

**Keep rules**, to a fixpoint:

1. **Roots:** `main`; `// distill: keep`; `--keep`; anything referenced from
   `asm` or by `alias`, `section`, `used`, `constructor`, `destructor`, `weak`,
   `visibility` attributes (an `asm` or attribute form `distill` can't
   interpret disables pruning); replacement `operator new`/`delete`; pinned and
   never-removed families; the root kinds above; **every variable with static
   or thread storage duration** (namespace scope, static data member, or function-local
   `static`/`thread_local`, the last through its enclosing function) **not on the
   inert whitelist** (R3-3). The whitelist is positive: a variable of scalar
   type (arithmetic, enum, pointer) or array of scalars, whose initializer is
   absent (zero-initialization) or a constant expression built only from
   literals, enumerators, other whitelisted `const`/`constexpr` variables,
   `sizeof`/`alignof`, and built-in arithmetic, comparison, logical, bitwise,
   conditional and cast operators — no function calls of any kind (not even
   `constexpr`), no `++`/`--`, no assignment, no lambdas, no temporaries, and
   no macro from a system header other than the numeric-limit and constant
   macros of `<climits>`, `<cstdint>`, `<cfloat>` and `<cstdio>`'s `EOF`
   (their judge replacement lists can't be seen, R4-H). The type must be
   spelled with builtin type keywords, or be a *syntactic* alias chain: each
   link a user `typedef`/`using` whose right-hand side is spelled with builtin
   type keywords or names the next link directly — no templates, `decltype`,
   dependent or member types anywhere in the chain (R5-2) — or `std::size_t`,
   `std::ptrdiff_t` or an `<cstdint>` type.
   Everything else, including every class-type variable whatever its
   constructor and `constinit` or `constexpr` status, is a root. A
   function-local variable without static storage is never a root by itself.
2. **Name rule:** a family is kept if one of its indexed names appears as an
   identifier token in the kept text outside the family itself (kept
   families, macro definitions, unparsed branches, all never-removed text).
   Literal suffixes count as the name of their `operator""`. Comments and
   string literals don't count. The rule reads source tokens; names that
   only pasting produces are found by rule 4 in the parse, and pasting that
   involves predefined macros is excluded by the preconditions.
3. **Association rule:** a non-member function or specialization family is
   kept if its signature or template arguments name a kept user record or
   enum.
4. **AST edges:** every declaration referenced from the AST of a kept
   family. Covered constructs are enumerated and tested (D3-1-5):
   expressions, types, base classes, default arguments and default template
   arguments, constraints and requires-expressions, implicit special members,
   constructors, destructors and conversions called implicitly, template
   patterns and their known instantiations (instantiations map to their
   primary; user specializations keep their own identity), and the instantiated
   bodies of system templates traversed for references back to user
   entities. Callbacks follow from these edges (a function passed as a
   value is referenced where it is passed).
5. **Dependent names** (D3-2-4): for every unqualified dependent call name
   in a kept user template pattern (including names produced by user-only
   pasting, from the AST), every user non-member family with that name
   anywhere in the translation unit is kept, hidden friends and later
   declarations included, without filtering by Clang's viability or the
   instantiations it saw. A construct the traversal doesn't support
   pins its family; if its dependencies can't be bounded, pruning is disabled.

**Static invariant:** rule 2 on the final rendered text: no removed family's
indexed name appears as an identifier token. It catches range and editing
bugs without a compiler; it is not an equivalence proof, and it can't see
pasted names.

**Measure before refining:** the report gives kept bytes per rule; D3 runs on
the user's library before any rule is made finer, measuring separately the
retention caused by operator roots, using-declarations, class-type globals,
the ADL names, name collisions (`using namespace std` makes `size`, `get`
and `data` common), association, and whole-family aggregation, unique bytes
and transitive causes apart (D3-1-7). Retaining most of a library is an
acceptable first result; rules are weakened only with a soundness argument.

Clarifications from D3a (implementation, reviewed):

- **Dependent member and qualified lookup** (`x.f()`, `x.template f<T>()`,
  `M::op`, `T::value`, `typename Tr::type`) need no extra rule and don't
  disable pruning: members go only with their whole record (out-of-line
  members and member specializations included), and the record or enum that
  carries the lookup is reached through the type carriers (template arguments
  spelled in kept text, AST edges of seen instantiations, return and alias
  chains, kept bases). A selection between user types by a system trait
  (`std::conditional_t<sizeof(long) == 4, A, B>`) names every candidate in
  kept text. Template arguments can't name a namespace.
- **Pragmas:** source pragmas stay and pin a family they cross; only the
  audited ones allow pruning: `GCC optimize`, `GCC target`,
  `GCC push_options`/`pop_options`/`reset_options`, `GCC diagnostic`,
  `clang diagnostic`, `pack`, `message`, `warning`, bare `once`. They change
  code generation, diagnostics or layout, never which declarations lookup
  needs. Any other pragma, and any `_Pragma`-generated one, disables pruning.
- **Supported forms are versioned:** the pruning-policy version documents the
  supported attributes, AST forms and semantic options; an unsupported one
  disables the whole analysis (e.g. symbol-bearing attributes, `asm`,
  `va_arg`, GNU statement expressions, computed goto, coroutines).
- **Paste provenance** may be over-approximated by the whole enclosing macro
  invocation; uncertainty disables pruning.
- **Inertness:** the whitelist is an upper bound; the implementation
  recognizes a positive syntactic subset (no constant evaluator) and reports
  the rest as not inert.
- **Ownership:** a shared syntactic unit has one physical owner and several
  unioned retention obligations; bytes are counted once.
- **Keep requests** bind to physical declaration occurrences; a request that
  can't be bound disables pruning, never drops the request.
- **Attribution:** family-owned and structural bytes are reported apart; the
  primary cause is a deterministic policy attribution, not a minimal proof.
- **Registry:** an unverified row is kept conservatively with its
  verification status; it never justifies removing a same-named user hook.
- **Parse:** the extractor uses Clang's cc1 directly; it sets the driver's
  parsing defaults explicitly (`-fgnuc-version=4.2.1`, exceptions, math
  errno, deprecated macros) and registers builtins before parsing.

Clarifications from D3c and D3d (implementation and first measurement, reviewed):

- **Registry editions:** the A6 registry audits C++11, C++14, C++17, C++20 and
  C++23 (N3337, N4140, N4659, N4861, N4950), GNU spellings normalized to their
  edition; other editions (e.g. `c++2c`) disable pruning until audited.
- **Attribute roots:** an interpreted attribute (`constructor`, `destructor`,
  `used`, `section`, `weak`, `visibility`, asm labels) roots its declaration's
  family; `alias` also roots its resolved user target. `asm` operands and
  symbols that resolve to user families are roots; uninterpretable forms or
  unresolved symbols disable pruning.
- **Parameters and expression-local declarations** (requires-expression and
  lambda parameters, init-captures) belong to their lexically enclosing family
  and add no indexed names.
- **Checker tests** probe each named check independently; a case may be caught
  by more than one. A `none` case passes both invariants, the build and the
  requested sample runs, and documents a limit.
- **Measurement** attributes unique family bytes to the immediate retaining
  rule, structural bytes apart, and records attempted candidate sizes even
  when the written artifact fell back.

### 4.8 Rendering

- Edit ranges are byte ranges in the immutable `flat.cpp` buffer, covering
  complete syntactic declarations with their template prefixes, attributes,
  declarators and terminators (D3-1-9). A range that begins or ends inside a
  macro invocation, shares one with retained text, or touches a retained token
  or source directive, pins every family it touches.
- The output is rendered in one pass from `flat.cpp` plus the deletions.
  Deletions may leave other tokens on a line; a line is removed only if a
  deletion covers all its tokens; a comment is removed only if it lies
  inside a deleted range.
- `#line` markers (§4.4) are recomputed from the D2 provenance after deletion
  and inserted only at line starts outside directives, comments and raw
  strings.
- **Rendering invariant** (D3-1-11): the output is lexed again; every
  surviving token must have its intended logical file and line, and the
  retained source directives must keep their token sequence and order.
  Failure rejects the candidate.
- `--strip-comments` (D3-1-10) is part of rendering every artifact (`raw`,
  `flat`, `pruned`) and keeps token spellings, directive boundaries and
  logical lines: a comment outside a directive becomes one space, or its
  newlines for a multi-line comment; a comment inside a directive's logical
  line (including one spanning lines) becomes one space, and the following
  line is resynchronised with a marker. Where that isn't possible the comment
  stays.
- All checks run on the exact bytes that are written (D3-1-12): every
  transformation, comment stripping and markers included, finishes before an
  artifact is checked; a fallback is written only in its own checked form.
  `-o` writes a temporary file and renames it after the checks; stdout gets
  nothing before the checks finish.

Clarifications from D3b (implementation, reviewed):

- **Directives in the rendering invariant:** a retained source directive
  keeps its token sequence, boundaries, order and logical starting line; its
  internal physical lines may contract when a multi-line comment becomes a
  space. Other surviving tokens keep their exact logical file and line.
- **Comments in conditional directives:** a multi-line comment inside a
  conditional directive stays as is, since a resynchronizing marker after a
  skipped group can't restore `__LINE__` for a later `#elif`.
- **`__LINE__` and `__FILE__`** are location facts, certified by the
  rendering invariant, not predefined values: their use doesn't trigger the
  predefined-macro precondition (§1.1 already excludes `__FILE__`'s text).
  Explicit `#define`/`#undef` of them stays an ordinary binding fact; pasting
  them from system macros still disables pruning. No other builtin is exempt.
- **Byte accounting** measures unique input source bytes per rule, family
  ownership and structural text apart; it is not the output size.
- **`--keep`** matches the unqualified namespace-scope index spelling, all
  occurrences; a name with no indexed declaration is an error (exit 1), also
  with `--no-prune`, and a keep request that can't be checked fails rather
  than being ignored.
- **Samples** (internal API until D4): ordered named stdin files; each checked
  build's runs are cached; execution is skipped when the build's target can't
  run on this host. `-Ofast` disables pruning (cc1 doesn't imply the driver's
  fast-math options).

### 4.9 Report

Header: result (artifact, verification level, runtime status), the
configuration used per artifact and the facts it satisfied, profile and its
date, option classes, the assumptions (A1–A6) the output relied on. Then one
line per conditional directive (`#if`/`#elif`/`#else`/`#endif`:
`resolved false (ONLINE_JUDGE: profile)`, `kept (unknown: __has_include)`,
`dead region` for directives inside removed text; `#else` reports
reachability, `#endif` whether its block was resolved or kept; generated
`#pragma once` guards have no original location but count as user code), one line per family with fate and reason (`root: not inert
(class type)`, `name "lcm" at main.cpp:12`, `associated with Mint`,
`never removed: operator`, `never removed: ADL name (A6)`, `referenced by …`,
`pinned: …`, `removed: outside the retention closure`), kept bytes per rule
(primary cause and transitive bytes apart), locations in the original files.
The header also names the parse configuration, every pruning-disable
reason, the pruning-policy version, the ADL registry version and the dialect
and extension coverage used (D3-2-7); A6 is listed whenever a family was
removed; a failed candidate's analysis is reported but certifies nothing
about the artifact written.

D2e: the report keeps the resolution's own provenance separate from the
facts each configuration's rerun observed (matching decisions need not use
equal values); an unreachable `#else` may report `dead region`; a rejected
`flat` keeps its decisions in the report beside the rejection, and they
certify nothing about the `raw` that was written.

## 5. Build and release

### 5.1 Code layout and language

```
src/
  core/   command/, probe/, profile/, inline/, resolve/, prune/, render/, validate/, process/, report/
  cli/    main.cpp
data/     judges/*.toml, extensions/*.txt   (embedded in the binary)
tests/    cases/<name>/  runner/
ci/       build, audit and list-generation scripts
.github/workflows/
```

C++20, CMake ≥ 3.24, exactly one pinned LLVM version, no compatibility layer.

### 5.2 LLVM

- Pin an exact release, chosen in D1: the newest stable (23.1.5 at the time of
  writing) if it parses current libstdc++ on every platform.
- D1 audits the official archives (static libraries, headers, CMake exports,
  CRT, deployment target, glibc baseline). Official 23.1.5 archives exist for
  Linux x86_64 and arm64, macOS arm64 and Windows x86_64, none for macOS
  x86_64. Where they fit, use them; elsewhere build from source.
- Source builds: `clang` only, Release, no LTO, no debug info, no tests,
  native target only, bounded link parallelism, optional dependencies off.
  The installed SDK is published as a release asset on `llvm-<version>-<n>`,
  split if over 2 GiB, keyed by revision, toolchain, architecture, CRT and
  deployment target, with the matching `llvm-project` source archive.
- Linux: LLVM and `distill` are built inside `manylinux_2_28`.

### 5.3 Self-contained binary

- Clang's resource headers are embedded, served through an in-memory VFS
  installed before driver and frontend setup, resource directory set
  explicitly.
- Linux: static libstdc++ and libgcc, glibc ≥ 2.28. macOS: the deployment
  target is the oldest macOS the release is actually run on in CI (R3-14):
  `macos-14` today, so 14.0. Windows: MSVC, `/MT`.
- CI audits every binary's dynamic dependencies against an allow-list.

### 5.4 CI and release

- **Smoke test on every platform** with a real GCC, binary moved away from the
  build tree: Linux system g++, macOS Homebrew GCC, Windows MSYS2 MinGW g++;
  Windows adds paths with spaces and non-ASCII characters.
- **Minimum-platform runs** before each release: the Linux binary in
  `almalinux:8` (glibc 2.28) with its own g++; macOS on `macos-14`.
- PR workflow: full suite on Linux x86_64 with g++ and with
  `x86_64-w64-mingw32-g++` (Codeforces profile, build checks only); smoke
  tests elsewhere. Required checks join the `main` ruleset once the workflow
  exists. Claude merges green PRs.
- Release on a `v*` tag: build, audit, smoke-test, publish binaries with
  checksums, `LICENSE` (GPL-3.0), LLVM's license and notices, the LLVM
  revision; corresponding source: the tag's source archive and the
  `llvm-project` source archive with the SDK, linked from each release.
- Runners: `ubuntu-24.04`, `ubuntu-24.04-arm`, `macos-15` and `macos-14`
  (arm64), `macos-15-intel` (x86_64; announced until August 2027; afterwards
  cross-compiled on arm64 and smoke-tested under Rosetta 2), `windows-2025`.

## 6. Testing

- **Adversarial cases first**, one per counterexample in R1–R3, each
  asserting the right result even where every candidate compiles (R1:
  compiler-branch overloads, `V __GNUC__`, `MODE`, headers in compiler
  branches, `NDEBUG` + `<cassert>`, X-macros, `__COUNTER__`, virtual members,
  SFINAE fallbacks, ADL operators, `std::hash`; R2: `__has_include` of a user
  header, re-included guards after `#undef`, `__LINE__` after deletion,
  `sizeof(long)` initializers, generic `operator+` with `requires`,
  `ONLINE_JUDGE` main; R3: `#ifdef assert` after `<cstddef>`, `M_PI` after
  `<math.h>`, `MODE` after a vendor header, `int unused = ++count`,
  implicit-constructor and `constinit`-destructor globals, generic
  `operator+` on `std::pair`, `CAT(chosen, 1)`, computed system includes,
  `__builtin_LINE`, `_WIN32` main with a Linux compiler, a cross compiler with
  the wrong GCC version, `-Wp,-include`).
  R4: a standard header redefining `__GNUC__` (fixture), `__builtin_LINE`
  in both branches, guards included twice, `#undef __clang__`, an omitted
  non-reserved predefined macro, recursive `pop_macro`, a system default
  argument with `__builtin_LINE` after a splice, compiler-dependent type alias
  and `INIT` macro for a global, a `using namespace` needed by an unparsed
  `requires`, a system macro calling a user function, a weak library hook;
  R5: `JOIN(pick, __GNUC__)` in a `requires`, `Select<__GNUC__>::type`
  global, a standard-header fixture macro calling a user function.
- **D3 pruning cases** (D3-1-13), each asserting retention or refusal, not
  only a passing build: the LP64/LLP64 `iter_swap` ADL case, a pasted
  predefined name (`CAT(__GNUC, __)`), a multi-line comment inside a
  `#define` under `--strip-comments`, a generated `#line` inside a removable
  function, a `#define` inside a removable-looking function, function-local
  vs static-storage variables, concepts, hidden friends, explicit
  instantiations, specializations of system templates, `__LINE__` after a
  deletion on a shared line, and forced bad removals that build and match
  samples (documented checker limits); `JOIN(pick, BUFSIZ)`; a dependent
  call found by ADL on a later declaration; structured-binding `get`,
  comparison, error-code, `from_stream` and valarray hooks; specialization
  customization (`hash`, `formatter`, `tuple_size`, variable templates); a GNU
  extension header (refused or covered); `--strip-comments` with
  `// distill: keep`. Cross-target cases assert retention or refusal
  directly. Fixtures that violate an assumption
  are labelled as such.
- **Resolution cases:** `#if` tables with no profile and with each profile;
  the preprocess check (§4.6) runs on all of them.
- **Equivalence:** every case built and run per §4.6; a baseline that doesn't
  build fails the test.
- **Checker tests:** a testing hook forces a bad removal; each case states
  which check must catch it, or that none can (a documented limit).
- **Port of caide's 81 cases** (the user's fork; measured since D3c), re-reviewed one by one.
- **Compatibility matrix:** bundled Clang × GCC versions on Linux, macOS
  (Homebrew GCC) and MinGW, parsing `<bits/stdc++.h>` in each dialect.
- **Corpus:** the user's library and solved problems, from D3 on.
- Every test runs under a hard timeout.

## 7. Milestones

| M | Work |
|---|---|
| D0 | This design: Codex round 4, user approval |
| D1 | Distribution spike (approved, running): official-archive audit; LLVM builds on all five runners with measurements; relocated stub parsing `<bits/stdc++.h>` with embedded resource headers against real GCC; dependency audit |
| D2 | Command model, probe, process containment and limits, profiles (researched), extension lists, text inliner, resolver, validation configurations with the preprocess check, test runner, adversarial preprocessing cases; `--no-prune` usable |
| D3 | Pruning: data model, keep rules, ADL registry, rendering, static and rendering invariants, report; the D3 pruning cases and checker-limit tests of §6, with execution comparisons in the test runner (D3-2-6); corpus measurement of kept bytes per rule |
| D4 | `--verify-run` in the CLI, the remaining checker tests, caide case port, compatibility matrix |
| D5 | Fixes from the corpus; release workflow, README, licenses, first tagged release |
| D6 | §4.3.1: profile names and aliases, judge probe and check; new profiles for Library Checker, yukicoder, Luogu, Kattis, CSES, QOJ and Timus |
| later | Repair loop; macro pruning; record member pruning; finer name matching; removing more operator families once a rule is proven |

## 8. Carried over and rejected

Carried over (caide §7): side-effecting globals are roots (now: every
variable not on the inert whitelist); records kept whole; no size threshold; a
real compiler checks every output; unused system includes stay;
`ONLINE_JUDGE` is a judge fact, now from profiles.

Rejected: Rust; USR-based equivalence; one generic "remove unreferenced
declaration" rule; modules or IR comparison; promising sub-second checks
(caide §8). New: several LLVM versions at once; a library API; resolving `#if`
with the local compiler's macros; preprocessor-based flattening; include
hoisting and deduplication outside resolution; several `.cpp` files; repair in
the first version; deciding roots with any constant evaluator; writing
unchecked files; accepting options by prefix; pruning with unknown
conditionals left in user code; line markers only on demand.

## 9. Open questions

None for the user (Q1–Q5 answered 2026-11-15). D1 may raise more (official archives vs own builds); D3's measurements decide
whether the name rule and the never-removed rules need refining.
