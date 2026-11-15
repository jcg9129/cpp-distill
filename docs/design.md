# cpp-distill design

Status: **approved v7**, 2026-11-15 (user): v6 plus the §4.3 amendment (judge
command, build provenance, special builtins, host-dependent options, A5),
challenged by Codex in two rounds (`docs/reviews/2026-11-15-codex-profiles-r1.md`,
`-r2.md`, cited as *P1-n*, *P2-n*). Opus drafted v1; Codex
challenged v1–v5 (`docs/reviews/2026-11-15-codex-design-r1.md` … `-r5.md`,
cited as *R1-n* … *R5-n*). Round 5 found no blocker for D2; v6 closes its two
D3 holes (§4.7: predefined macros used as values, template-selected aliases).
Before D3 starts, Codex gets a short round on the v6 pruning rules.

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
(64 MiB), macro-expansion steps, wall clock per step and per run. Clang
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
build = { package = "msys2:mingw-w64-x86_64-gcc=14.2.0-1" }   # or { recipe = "<url>" }
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
**approximate**.

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
`__GCC_CONSTRUCTIVE_SIZE`, …). The snapshot step evaluates the effective
command (later options and macro operations included) for every CPU
configuration the model allows and joins the results per name: absent
everywhere is *undefined*, the same definition everywhere is known, present
everywhere with differing definitions is *defined, definition unknown*,
otherwise *unknown*. `-mtune=native` alone varies only tuning-derived macros. A
sweep over the compiler's `-march=` values is a regression check of the model,
not its proof.

**Facts** a profile supplies, each with its provenance recorded:

- reproduced build: every name as the snapshot, the special-builtin probes and
  the native dependency model give it;
- approximate profile: only allow-listed facts (compiler identity, target OS
  and architecture, dialect, type sizes and limits, character signedness) are
  taken from its snapshot; every other reserved name is *unknown* (P2-1);
- every profile: an unlisted non-reserved name is *undefined* (A5); an
  unlisted reserved name is *undefined* for a reproduced build (A4) and
  *unknown* for an approximate one.

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
  unknown.

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
every condition the resolution decided (R4-C). Derived source states are
recomputed by that rerun, not compared. Builds of
`raw.cpp`, `flat.cpp` and the candidate run in a temporary directory without
the user's `-I`/`-iquote` paths, so a missed inline is a build error.

| Artifact | Configuration | Checks |
|---|---|---|
| baseline | local (compile command as given) | builds; runs samples |
| `raw` | local | preprocess check against the original; builds; runs equal the baseline |
| `flat` | the first configuration that satisfies the resolution: a `check` compiler from the profile, else the local compiler with the profile's macro operations replacing local `-D`/`-U` | preprocess check `raw` vs `flat` in that configuration; builds; runs equal the original built in the same configuration |
| `pruned` | same as `flat` | builds; static name invariant (§4.7); runs equal `flat`'s |

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

### 4.7 Pruning

Runs on `flat.cpp` with the Clang parse of §4.2, **only if no unknown
conditional remains in user code** (R4-H, R4-I, R4-J) **and no predefined
macro is used as a value** (R5-1): if any identifier token in user code
outside `#if`/`#elif` conditions (including `#define` bodies) names a macro
that Clang's parse environment or the judge configuration predefines (both
sets known: Clang's from the parse, the judge's from the snapshot or the
facts), and the two disagree on its definition, or either set is incomplete
for it, pruning is disabled. Under these two conditions the user code Clang
analyses expands to the same tokens as the code the judge compiles, up to
system-header macros, which A2 (b) covers; there is no unparsed user text
whose dependencies would have to be guessed. Without a
profile, any compiler, platform or judge conditional (`#ifdef __GNUC__`,
`#ifndef ONLINE_JUDGE`) therefore leaves the result at `flat`; with a profile
snapshot such conditions resolve. Also disabled by an include of a system
header outside the standard-header list and by non-standard libraries
(§1.2).

**Data model:** *entities* (Clang declarations, user and system),
*retention families* (indivisible sets of user entities), *edit ranges*
(disjoint token ranges in `flat.cpp`, one or more per family). Overlapping
retention obligations are merged with union-find over families; source ranges
are never merged.

**Families:**

- a top-level declaration group (`int a, b;`, `struct S {…} s;`);
- a record with every member and its out-of-line member definitions;
- an overload set: all non-member functions and function templates with the
  same name in the same namespace, with their redeclarations;
- a template family: primary, partial and explicit specializations,
  deduction guides; a user specialization of a system template (`std::hash`)
  is its own family;
- an enum; a typedef or alias declaration; a namespace alias; a
  using-declaration or using-directive; a variable with its redeclarations.

**Conditional blocks:** after resolution only conditionals in system
headers remain, so user code has none (pruning precondition). Directives in
user code that remain (`#define`, `#undef`, `#pragma`, system `#include`) are
never removed; a family whose range crosses one is pinned.

Never removed in the first version: macro definitions, record members,
declarations produced by a macro invocation, namespaces (an empty
`namespace N {}` goes only if `N` no longer appears), **every user
non-member operator family** (R3-4), every namespace-scope
`using`-directive and `using`-declaration (lookup depends on them without
naming them, R4-I), every `extern "C"` function or variable definition
(the C library and other external code can call or reference them by name,
R4-K), and every function template named
`begin`, `end`, `cbegin`, `cend`, `rbegin`, `rend`, `size`, `data`, `empty`,
`swap` or `get` with a parameter of template-parameter type.

**Indexed names** (for the name rule): the names a family declares at
namespace scope: record, enum, typedef/alias, function, variable, template
and namespace-alias names, and unscoped enumerators. Member names, template
parameter names and local names are not indexed.

**Keep rules**, to a fixpoint:

1. **Roots:** `main`; `// distill: keep`; `--keep`; anything referenced from
   `asm` or by `alias`, `section`, `used`, `constructor`, `destructor`, `weak`,
   `visibility` attributes; replacement `operator new`/`delete`; pinned and
   never-removed families; **every variable not on the inert whitelist**
   (R3-3). The whitelist is positive: a namespace-scope variable of scalar
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
   constructor and `constinit` or `constexpr` status, is a root.
2. **Name rule:** a family is kept if one of its indexed names appears as an
   identifier token in the kept text outside the family itself (kept
   families, macro definitions, unparsed branches, all never-removed text).
   Literal suffixes count as the name of their `operator""`. Comments and
   string literals don't count.
3. **Association rule:** a non-member function or specialization family is
   kept if its signature or template arguments name a kept user record or
   enum.
4. **AST edges:** every declaration referenced from the AST of a kept family,
   with implicit code and template instantiations traversed, and through
   system entities (the instantiated bodies of system templates are traversed
   for references back to user entities). Instantiations map to their primary.

**Static invariant:** rule 2 on the final rendered text: no removed family's
indexed name appears as an identifier token. It catches range and editing
bugs without a compiler; it is not an equivalence proof.

**Measure before refining:** the report gives kept bytes per rule; D3 runs on
the user's library before any rule is made finer.

### 4.8 Rendering

- Edit ranges are token ranges in `flat.cpp`. A range that begins or ends
  inside a macro expansion, or touches a retained token or directive, pins
  the family.
- The output is rendered in one pass from the immutable `flat.cpp` text plus
  the deletions; a line is removed only if a deletion covers all its tokens; a
  comment is removed only if it lies inside a deleted range.
- `#line` markers (when §4.4 requires them) are recomputed at render time.
- `--strip-comments` replaces each comment by one space, or by its newlines
  for multi-line comments.
- All checks run on the exact bytes that are written.

### 4.9 Report

Header: result (artifact, verification level, runtime status), the
configuration used per artifact and the facts it satisfied, profile and its
date, option classes, the assumptions (A1–A4) the output relied on. Then one
line per `#if` (`resolved false (ONLINE_JUDGE: profile)`, `kept (unknown:
__has_include)`), one line per family with fate and reason (`root: not inert
(class type)`, `name "lcm" at main.cpp:12`, `associated with Mint`,
`never removed: operator`, `referenced by …`, `pinned: …`, `unreachable`),
kept bytes per rule, locations in the original files.

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
- **Resolution cases:** `#if` tables with no profile and with each profile;
  the preprocess check (§4.6) runs on all of them.
- **Equivalence:** every case built and run per §4.6; a baseline that doesn't
  build fails the test.
- **Checker tests:** a testing hook forces a bad removal; each case states
  which check must catch it, or that none can (a documented limit).
- **Port of caide's 79 cases**, re-reviewed one by one.
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
| D3 | Pruning: data model, keep rules, rendering, static invariant, report; corpus measurement of kept bytes per rule |
| D4 | `--verify-run`, checker tests, caide case port, compatibility matrix |
| D5 | Fixes from the corpus; release workflow, README, licenses, first tagged release |
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
