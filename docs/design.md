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

### 1.3 Trust assumptions (user, amended through 2026-11-18)

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
  predefines (`__GNUC__`, `_WIN32`, …). Amendment (user, 2026-11-18): for
  compiler/target identity names in `data/names/identity-macros.txt`, they
  also never define a name absent from the compiler's initial environment,
  subject to the registry's library coverage. All selected libraries must
  cover the name; an absent inventory uses the intersection of the supported
  libraries. MinGW-w64 coverage excludes its six synthesized `_M_*` aliases
  and `_WIN64`. Audited target architecture and data-model names are included;
  glibc coverage excludes `__ARM_ARCH` and `__thumb__`, and macOS coverage
  excludes `_LP64` and `__ILP32__` (see the registry's audit). This preserves
  known external initial facts, including
  profile command operations, not facts written by source `#define`/`#undef`;
  (b) an expansion of one of their
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
- **A7** (user, 2026-11-18) The program's observable behaviour does not depend on
  constant evaluation reaching an implementation limit (constexpr call depth,
  loop/operation limits, template instantiation depth); in particular no
  constraint, SFINAE or if-constexpr outcome is decided by limit exhaustion or
  by compiler evaluation caches. Every pruned artifact relies on A7, including
  deletion of unused enums or templates that could otherwise seed an
  evaluation cache. The report lists A7 for every pruned artifact.

- **A9** (L1c, user, 2026-11-18; widened 2026-11-23) In default library
  configuration (§4.3.3), the `__cpp_lib_*` macros and the approved standard
  object-like integer macros (§4.3.3) defined after any sequence of standard-list
  includes equal the union of the per-header snapshots, with their recorded
  values: no library header changes such a macro, or a compiler predefine its
  captured definition depends on, after another header has defined it.
  Different values for a name make that name unknown. The report lists A9
  whenever a resolution relies on these header-derived facts.
  `--no-assume=library-macro-values` keeps A9 for `__cpp_lib_*` only.

- **A11 (`instantiation-effects`)** (user, 2026-11-20) The program's observable behaviour and its compilation success do not depend on whether the definition of a function friend declared in a template has been instantiated (friend injection / stateful metaprogramming, cf. CWG 2118), nor on the destruction of template parameter objects, of constexpr variables and the temporaries whose lifetime they extend, or of block-scope variables with static or thread storage duration whose declaration control never reached.
  By default every pruned artifact relies on A11; reports print
  `A11 instantiation-effects` alongside A7. `--no-assume=instantiation-effects`
  disables this assumption while keeping pruning enabled (§4.7).
- **A12 (`library-spelled-members`)** (user, 2026-11-20) The judge's standard library headers call, name or take the address of a member of a user type only through an identifier spelled in, or produced by macro expansion of, the library headers the translation unit includes on the worker, or spelled in the headers of a scanned library build, or as an operator function, conversion function, constructor or destructor.
  Every enabled member-pruning analysis reports `A12 library-spelled-members`,
  even when it removes no unit, consistently with A11. The assumption is disabled
  by `--no-assume=library-spelled-members`; only whole-family pruning then remains.
  An unavailable worker raw/expanded header inventory also disables member pruning
  with a reason, without disabling whole-family pruning (§4.7).

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
| `--no-assume=<name>` | disable a switchable assumption while keeping pruning; repeatable, comma lists accepted; `instantiation-effects` (A11), `library-spelled-members` (A12) and `library-macro-values` (A9 standard macros) are switchable; any other name is a usage error (exit 1) listing switchable names |
| `--strip-comments` | remove all comments |
| `--no-line-markers` | omit generated `#line` markers from a `pruned` result when no observation of source positions is possible (§4.4); otherwise keep them and report why |
| `--compact-whitespace` | opt-in line-edge/empty-line trimming; implies `--no-line-markers` and uses its source-position proof, including columns; otherwise unchanged with a reported reason (§4.8) |
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
| modelled | `-I`, `-iquote`, `-isystem`, `-D`, `-U`, `-std=`, `-m32`, `-m64`, `-funsigned-char`, `-fsigned-char`, `-fshort-wchar`, `-fno-exceptions`, `-fno-rtti`, `-fmodules`, `-fopenmp`, `-march=`, `-mtune=`, `-mcpu=` | represented in the configuration object (§4.6); explicit supported translations feed the Clang parse. Modelled does not imply translated: modules, OpenMP and CPU options currently disable pruning as untranslated semantic options. |
| harmless | `-O0`…`-O3`, `-Os`, `-Og`, `-Ofast`, `-g`, `-g0`…`-g3`, `-W<name>` and `-Wno-<name>` for warning names (not `-Wp,` `-Wl,` `-Wa,`), `-w`, `-pedantic`, `-pedantic-errors`, `-pthread`, `-static`, `-static-libstdc++`, `-static-libgcc`, `-s`, `-lm`, `-lpthread`, `-lstdc++`, `-fdiagnostics-color=…`, `-fmax-errors=…`, `-pipe`, `-fsanitize=…` | passed to every build unchanged |
| libraries | other `-l<lib>`, `-L<dir>` | passed to builds; pruning disabled (R4-K) |
| controlled | `-o`, `-MD`, `-MMD`, `-MF`, `-MT` | `-o` replaced per build by a temporary path; dependency outputs dropped |
| rejected | `-c`, `-S`, `-E`, `-include`, `-imacros`, `-Wp,…`, `-Xpreprocessor`, `-Wl,…`, `-Xlinker`, `-Wa,…`, `-Xassembler`, `-save-temps`, `-B…`, `--sysroot`, `-isysroot`, `-M`, `-MM`, more than one input | error |
| unmodelled | anything else | passed to builds; disables resolution and pruning (only `raw.cpp`), with a message naming the option; exact profile-origin `inert` occurrences have a preprocessing permission exemption (§4.3), while the semantic pruning gate remains |

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
and nothing else about the judge is known. Local probes do not establish judge
compiler/platform facts; pruning code with target-dependent conditionals needs
a profile supplying the required facts, otherwise the unresolved conditions disable it.

**Command** (P1-5, P1-6, P2-6). `flags` is parsed structurally by the §4.1
command model with the same rejected list (no `-include`, `-imacros`, `-Wp,…`,
`-specs=`, `-B…`, …); an unmodelled option is a profile error unless the
profile lists it in `inert` (options it documents as not affecting
preprocessing, such as `-fconstexpr-depth=…`). This is a preprocessing-only
guarantee for exact occurrences originating in the selected profile's `flags`;
the same unmodelled spelling in a local command still disables permission.
It does not prove safe omission from the AST dependency parse: untranslated
semantic options independently disable pruning (§4.7). `-D`/`-U` stay an ordered list
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

A snapshot-only `winlibs` value fully matches
`winlibs:[A-Za-z0-9][A-Za-z0-9._+-]*/[A-Za-z0-9][A-Za-z0-9._+-]*\.(zip|7z)@sha256:[0-9a-f]{64}`:
an official `brechtsanders/winlibs_mingw` release tag, asset basename and archive
blob digest. The scoped form fixes the repository and avoids arbitrary download
URLs. CI hashes the downloaded bytes before extraction. Like `homebrew`, it is
forbidden in `build`, records `reproduced: no`, and does not identify the judge's
compiler revision. Timus uses this form for an approximate WinLibs snapshot.

**Snapshot** (P2-3): the macro state at the first token of an empty source
under `-x c++ -std=<std> <flags>` (macro operations included, in order),
recorded per name with its provenance: compiler builtin, startup header
(`stdc-predef.h`, detected from the line markers of a separate `-E` run, since
`-H` starts tracing after it), or command operation. Any other header
processed before the source is a profile error.

Header-defined library feature macros have a separate per-header CI snapshot,
configuration audit and cross-build agreement file (§4.3.3). They do not add
source-entry compiler facts.

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
  taken from its snapshot, with the conditional additions below; every other
  reserved name is *unknown* (P2-1);
- every profile: an unlisted non-reserved name is *undefined* (A5); an
  unlisted reserved name is *undefined* for a reproduced build (A4) and
  *unknown* for an approximate one.

**Approximate GCC additions** (user, 2026-11-18), audited in
[`reviews/2026-11-18-approximate-allowlist.md`](reviews/2026-11-18-approximate-allowlist.md):
for GCC 13–16 snapshots, the finite non-GCC identity list in
`approximate-allowlist.txt` licenses initial **absence only** (`_MSC_VER`,
the listed `_M_*`, Clang, Intel, CUDA and Emscripten identities). Unexpected
positive definitions remain unknown. This relies on A4's GCC/command assertion;
it does not rule out downstream specs or unrecorded command definitions.
Existing A2 library restrictions still apply after headers, notably MinGW's
six `_M_*` aliases. Explicit profile `-D`/`-U` operations retain precedence.

For the ten `__INTn_C`/`__UINTn_C`/`__INTMAX_C`/`__UINTMAX_C` helpers,
only positive compiler-builtin definitions matching GCC's mechanical rule
are admitted. Require known, allow-listed, compiler-builtin type facts from
the same snapshot: exact-width and matching least-width types for `n`, or
the corresponding MAX type. Recognize canonical standard integer spellings;
unsigned char/short additionally need their width and int width to apply
integer promotions. Require GCC's single `c` parameter and exact `c` or
`c ## SUFFIX` replacement. Missing, overridden or inconsistent prerequisites,
unsupported types, missing helpers and inconsistent definitions stay unknown.
The derivation never rewrites the snapshot. It establishes consistency
conditional on already trusted type facts, including inferred ABIs, and
does not upgrade the profile to reproduced. `--judge-check` remains
presence-only for these function-like helpers and does not probe every newly
admitted absent identity; no additional detection coverage is claimed.

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
     lines are diagnostics and never validate A1 inventories (J1-9). The L1c
     companion (§4.3.3) additionally prints and compares library integer macro values
     after each header in a separate TU; it remains diagnostic only.
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

### 4.3.2 Profile maintenance (drift check)

Added 2026-11-17 (user), after a Codex review of the first draft. Three
separate claims are monitored separately, because success in one says little
about the others: the judge still matches the profile (**evidence**), the
pinned toolchain still reproduces the shipped snapshot (**reproduction**), and
the pinned artifacts are still obtainable (**availability**). None of them
advances `updated`, which stays the date of the last human review (A4).

**No binary mirror** (user, 2026-11-17). Republishing judge compilers is
GPL/LGPL redistribution with full corresponding-source obligations for every
mirrored item (including all Docker layers). Instead acquisition uses durable
upstreams (exact Ubuntu packages through `snapshot.ubuntu.com` at a recorded
timestamp, registries by digest, GNU archives) and records exact locks and
source links. A vanished artifact is an availability issue: shipped snapshots
stay valid (they are committed); the profile is re-snapshotted from a
successor artifact by PR, or documented as no longer regenerable.

**Evidence watch.** `data/judges/watch/<profile>.toml` lists each evidence
source: URL, kind (`deployment`: what the judge runs; `availability`: a package
or release listing, never evidence of adoption), an extractor
(`ci/drift/extractors/`, tested against saved pages including changed, missing,
duplicated and challenge pages) and the expected structured facts (language
ID, compiler family and version, ordered compile arguments with exact macro
text, runtime/library notes), plus the original-language excerpt for review.
Moving deployment URLs are watched; pinned commit URLs stay as historical
evidence. Each fetch is classified: same facts; different facts (candidate
drift); extractor failure (zero or multiple matches, missing fields); fetch
failure (login/challenge page with HTTP 200); evidence unavailable (403,
timeout, rate limit). Only "same facts" counts as a pass. A source unavailable
for four consecutive weekly windows becomes a manual-review issue with a
deadline; it is never silently green.

**Availability.** Every pinned artifact is fully downloaded (registry index,
selected child manifest and blobs for images) and its digest verified; Ubuntu
packages through the pinned snapshot URL. Metadata reachability alone is not a
pass. A digest mismatch is an integrity alert.

**Reproduction** (monthly, and on demand). `profiles.yml` regenerates every
profile from a cold start; outputs must be byte-identical to the shipped files.
No normalization is applied (reruns of Kattis, Codeforces and both
Linux runner paths were byte-identical on 2026-11-17, runner paths included).
Expected outputs record native-model facts with the host-independent `native`
marker, so reproduction stays byte-exact without normalization.
Native-model names are already `unknown` in snapshots, so host CPUs do not
enter them; a sweep or host-check failure is a reproduction failure. Any other
difference, a skipped probe, or a missing artifact is a failure, never a pass.

**Schedule.** Evidence, availability and L1c library data (§4.3.3) weekly;
reproduction monthly; all by
`workflow_dispatch` too. Fetch, download and build jobs have read-only
permissions and treat fetched text and binaries as untrusted data (never
interpolated into shell or scripts). A separate reporting job alone has
`issues: write`.

**Reporting.** One issue per profile and claim (`drift: <profile>:
evidence|availability|reproduction|library`), plus one aggregated issue when a shared
upstream (Homebrew, GitHub, Docker Hub) fails for several profiles. The issue
body carries a machine-readable state block (consecutive failures per source,
last success), so history survives ephemeral runners. Comments only on state
transitions. Availability issues close automatically after a verified
recovery; evidence, reproduction and library issues stay open until a human closes
them. A cancelled, skipped or partial run changes no state. Runs are serialized
(`concurrency`).

**Heartbeat.** GitHub disables schedules in public repositories after 60 days
without activity, so the run cannot vouch for itself. The user-side signal is
independent of CI: `distill --judge X` prints a stderr note when the profile's
`updated` is older than 180 days, suggesting `--judge-check` (§4.3.1). It does
not change any fact or exit code.

### 4.3.3 Library feature-test macro facts (L1c)

Approved 2026-11-18 after two adversarial reviews; see
[`reviews/2026-11-18-library-feature-macros.md`](reviews/2026-11-18-library-feature-macros.md).
Part A supplies CI producers, comparisons and diagnostic tooling; part B
embeds the reviewed baseline and implements resolver header transitions.
Extended to **library header integer macros**; A9 widened by user decision on
2026-11-23 to cover the approved standard object-like integer macros.
This is an extension of the approved L1c design, with the same include transition,
default-configuration Boolean, agreement, union rule, invalidation and A9 provenance.
The embedded universe is `__cpp_lib_*` plus names in
`data/names/standard-macros.txt` that at least one captured header actually defines
as an integer. Usable definitions must expand through complete
object-like identifier chains to a single integer literal. Other replacements
have presence-only metadata; function-like macros also have presence-only metadata. Parenthesized
literals, unary minus, floating literals and expressions are not admitted: the
motivating `FLT_EVAL_METHOD` captures need only bare `0`/`2` literals. These are
header facts, separate from the source-entry compiler snapshot and identity registry.
`--no-assume=library-macro-values` disables all standard-macro header facts,
including absence and preservation across includes: those names become unknown
at a standard include as before this extension. `__cpp_lib_*` facts retain A9.
The report records that the standard-macro assumption is disabled, and its A9
resolution line distinguishes standard-macro use from feature-test-only use.

**Profile data.** For each profile, CI uses the same snapshot compiler and
exact `-x c++ -std=<std> <flags>` argv, including ordered macro operations.
For every C/C++ standard-list header, `bits/stdc++.h`, `ext/rope` and every
installed file below `ext/pb_ds/`, a fresh TU contains only `#include <H>`.
Its `-dM -E` output supplies the selected object-like names and values. A missing header,
a preprocessing failure and a successful header defining no selected macros
are distinct states; failed output never proves absence. Internal object-like
aliases (GCC 14+ `__glibcxx_*`) are expanded through the dump to their integer
values. Snapshots retain only integer values, plus `noninteger` and
`function_like` presence lists; neither list can prove absence or a value.
Preprocessing precedes auditing so non-feature standard names never observed as
integers need no detailed value audit. Their per-root `noninteger` lists retain
possible writes, including function-only names. If another build supplies an
integer candidate, these unaudited writes block absence and major independence;
all native inputs conservatively block reproduced absence for such names.
Dependencies on omitted names remain unrecognised. Selected names still retain
every write, including unsupported replacements and undefs, and every existing
pair, reset, taint, configuration and native-input rule applies.

`ci/profiles/snapshot.py` additionally produces `<profile>-library.json` beside
the existing outputs. `ci/extensions/scan.py` produces analogous build reports
using the same reachable-header closure as A1/A2. This includes GCC 13 through
its Ubuntu and WinLibs profile builds, GCC 14–16, current MinGW, and the older
AlmaLinux build; macOS libc-only rows are explicitly excluded. Compatibility
scans select a supported ISO dialect by GCC major; profile scans always retain
the profile's exact dialect and flags. Adding a profile or eligible compatibility
matrix row automatically adds a required input to the merger.

**A12 member inventory.** Generators 4 and 5 additionally raw-tokenize the complete
reachable-header set already computed for each eligible build, including libc,
inactive branches and macro definitions. Comments, character/string literals and
pp-numbers contribute no identifier names; line splices and universal character
names are decoded. The compact schema-2 file stores one sorted global
`member_names` union, plus each build's name count/digest, reached-header count
and `token_pasting` boolean. Full evidence retains per-build sorted names and
the header keys containing `##` or `%:%:`. Pasting is report-only, since the
worker already inventories actual expansions. The binary embeds the union and
reports its build count and generator; this is independent of resolver fact
selection and applies even for builds without reproduced profile snapshots.
Old generator-3/4 baselines remain accepted; generator 3 has worker-only member provenance.
Until CI data is imported, the old feature-only baseline supplies no new standard
macro values, so `FLT_EVAL_METHOD` after `<cfloat>` stays unknown.
Regenerate using `library.yml` (manual dispatch, no inputs; it calls profiles
with `profiles: all` and the complete eligible extension matrix), review
`library-generated`, and import its `library.json` byte for byte as documented
in `ci/library/README.md`.

**Configuration audit.** Analyse actual installed headers, not `version.def`
alone: lexical `#if`/`#ifdef`/`#ifndef` stacks, including nested inactive
branches and previous `#elif` tests controlling `#else`. This handles GCC 13's
distributed definitions and GCC 14+'s generated `bits/version.h`. For root H
(a fresh TU containing only `#include <H>`), each literal
(H, name, value) pair is config-independent only when every controlling
dependency is compiler-predefined (including `__cplusplus`/`__STRICT_ANSI__`),
verified include-guard/own-`#ifndef` plumbing, verified internal feature
requests, an explicit reviewed library-macro candidate, or the reviewed trusted
inputs below (user-approved 2026-11-19). Request and internal
value macros are followed through all writes reachable from H and their
conditions; a prefix alone grants no exemption. Undefined request alternatives
are known false only when installed writes establish their internal request
role. Include-edge conditions on **all** paths from H to the definition's file
propagate transitively, including conditional alternatives and cycles. Paths
from other roots cannot contribute. Classification is per root and literal
value; it is never a library-wide property of a name. Compiler-looking names
written in H's closure or overridden by command/startup definitions lose
compiler-input status. Trusted compiler definitions are retained so changes to
their replacements are detectable too.

A standard macro's value alias may terminate at an actual compiler predefine
from this capture's exact argv. Its recorded object-like identifier chain must
end in an integer literal; missing, locally written, overridden or native-volatile
links fail closed. Unusable alias chains still retain their transitive identifier
dependencies so reproduced snapshots cannot hide native inputs behind them. No worker or default-argv value is substituted. Compiler-origin
undef/redefine sequences retain the final compiler definition; command/startup
writes still revoke compiler-input status.

GCC `float.h` explicitly undefs `FLT_EVAL_METHOD` before its definitions, and
`cfloat` adds a fallback definition in another file. For a standard-list macro
only, the established sole-writer initial-undef rule licenses **observed positive
literal pairs**. When there are additional writer files, a reset prefix may
also be ignored when subsequent definitions in that same file cover every reset
path. The additional proof resolves complementary lexical branch atoms; it
does not evaluate configuration expressions. Other writer files are audited
separately. Every remaining definition and condition must still pass the audit.
All raw writes and per-root `reset_prefixes` remain in evidence. A later undef,
an uncovered reset path or unsupported replacement still invalidates the pairs.
This rule does not prove absence/identity for a header that resets the name.

Standard integer agreement additionally partitions each major's captures by
compatible target triple, dialect and ordered non-harmless argv. All captures
in that partition must independently justify the value; every capture of the
major must still observe the same value/presence with an available header. Thus a native
MinGW Timus capture cannot block or license a CSES Linux `-O2` value. Native
compiler inputs remain untrusted, including both evaluation-method predefines.
Scoped refinement adds positive values only; absence/identity proofs retain the
existing major-wide rules, including omitted-write blockers.
The compact baseline retains these optional `standard_scopes`, their root
classifications/agreements and each build's `standard_scope`. Feature macros
continue to use the complete major matrix. At embedding, a scoped row combines
its standard values with the existing major feature agreement; identical rows
fall back to the major row. The selector checks target, dialect and exact ordered
non-harmless flags before selecting a scope. Each build's `standard_inputs`
also records compiler definitions and source-entry absences needed by approved
standard facts. The selector compares these with the profile's raw snapshot
compiler facts (including provenance), without promoting them to general
source-entry facts. Default-library invalidation still protects against later
reserved-name or standard-macro writes. Old baselines without these fields keep
their existing major agreement and selection behavior.

The embedded candidate allowlist in `ci/library/audit.py` is exactly
`_GLIBCXX_FLOAT_IS_IEEE_BINARY32` and `_GLIBCXX_DOUBLE_IS_IEEE_BINARY64`.
Neither is assumed pure: every scanned definition/undefinition, replacement
and controlling condition must pass the compiler-only dependency audit before
it is admitted. Their installed definitions and conditions remain in the
review artifact. Build switches (`_GLIBCXX_USE_CXX11_ABI`, `_GLIBCXX_HAS_GTHREADS`,
`_GLIBCXX_USE_*`, `_GLIBCXX_HAVE_*`, threads, locale, etc.) and unrecognised
dependencies fail closed, except for the narrowly justified inputs below.
Native-model names are excluded from trusted compiler inputs under native flags. Unsupported
replacement forms and unresolved computed includes also fail closed; they
may reduce coverage and are visible in reports. Whole-file guards and nested
`#ifndef X` / `#define X` wrappers carry textual evidence. All installed writes must have the matching guard role and
identical file contents (aliases are allowed); conflicting writes,
undefinitions and command/startup overrides revoke guard trust. Only the
wrapper's actual negative test is exempted, never arbitrary positive/value
tests of the same name. This also handles C wrappers such as
`_GLIBCXX_STDLIB_H`, whose guard is inside an outer conditional, and whole-file
`_GLIBCXX_CMATH` guards surrounding `#include_next`. For a negative guard test
before entering another file, the audit proves that no guard writer is
reachable from H without that include edge. The guard is then unset on first
traversal in the fresh TU. Possible earlier/sibling inclusion defeats that
proof and remains untrusted. There is no unconditional promotion of library
configuration macros.

Unparseable files remain in the full audit with their reasons. A malformed file,
computed include or unsupported absolute include reachable from H taints every
feature/absence result for H; unreachable failures do not affect it. Recoverable
literal includes in a malformed file still contribute reachability. Literal
include operands and `__has_include`/`__has_include_next` arguments are never
macro-name tokens. The audit additionally trusts:

- The source-entry absence of `__STDC_VERSION__` in C++ mode and the user opt-in
  `__STDC_WANT_IEC_60559_TYPES_EXT__`, needed by GCC 13 `float.h`. These two names
  must actually be absent from the captured entry definitions, have no
  command/startup override, and have no write anywhere in H's closure. The
  default-library Boolean forbids intervening user writes. This finite rule
  licenses no other library build switch; `source_entry_absences` records it.

- Independent `__cpp_lib_*` and `__glibcxx_*` condition macros justified for
  the same root. SCC expansion follows conditions and request/value aliases;
  a grounding queue requires reachable writers and all lexical/value
  prerequisites, including include-edge conditions. Unanchored cycles and
  unsupported writes remain dependent.
- `_GLIBCXX_HOSTED` when every scanned build of the GCC major records value 1.
  A missing or non-hosted observation revokes trust for the entire major.
- Literal `__has_include` and `__has_include_next` queries when every build of
  the major agrees on presence or absence. Reproduced profiles use their own
  build's query set. Query keys preserve quote lookup and `include_next`
  position with source-relative identities. Missing/disagreeing evidence and
  computed operands remain untrusted.

`audit.trusted_inputs` and per-root `trusted_inputs` record the rules. Captures
retain hosted values, context-sensitive header-query observations and unfiltered
SCC requirements. The merger reapplies matrix-approved trust to those requirements
without rerunning the audit, retaining effective rules and build sets in full
evidence. Per-header preprocessing status remains independent.

Capture retains search-index/include-spelling keys, including symlink aliases.
Literal edges follow compiler search order, quoted-include directory and
`include_next` search position. Reading and include parsing are shared by
resolved file; condition identifiers are cached. Per-root inbound conditions
and integer alias values propagate with a changed-node queue. Request/value
dependencies are expanded by strongly connected component, with the grounding
proof above admitting independently justified library-macro conditions and
leaving unanchored cycles unrecognised. Tests check per-root propagation against
a small-graph oracle, request cycles exhaustively, compiler-call/header-snapshot behavior, and compact
round trips. Each capture logs discovery, audit, preprocessing, writing and
total times plus header-key and `-dM` probe counts. Each available requested
header is preprocessed once.

`ci/library/merge.py` requires reports from the complete profiles and extensions
matrices and never unions old facts into fresh results. Schema 2/generator 5
separates the compact baseline `data/library/library.json` from the uploaded
`library-evidence.json`. The latter retains complete captured build reports,
including candidate definitions, conditions, include paths, compiler inputs and
per-root/per-value classifications. The baseline retains build metadata,
per-header snapshots/status (with optional `function_like` presence-only names
that are excluded from object-value facts), per-major/root classification (`independent`,
permitted `values`, sorted dependency names) and per-header agreement
(`defined`, `absent`, `unknown` with short reasons). A function-like definition
of a name that is object-like in another capture is unknown, never evidence of
absence, for both major agreement and reproduced snapshots. Each compact build also
retains a full `evidence_sha256`: SHA-256 of its entire report encoded as ASCII
JSON with sorted keys, separators `(',', ':')` and no trailing newline. Any
evidence change therefore fails comparison even when facts do not change.
Captures store identical per-name/per-value audit records once in an optional
`audit.root_records` array; root macro maps reference capture-local integer
indexes. These indexes are not content addresses: the merger reapplies matrix
trust to their retained requirements. Old expanded generator-5 reports remain
readable with byte-identical baseline output. Raw captures and merged evidence
use compact JSON; the reviewed baseline keeps its one-record-per-line format.

The merger streams input JSON into a temporary SQLite store beside its output,
decoding one root at a time, rather than loading the entire matrix. Matrix trust
is persisted before classification. Evidence hashes and output stream canonical
stored root bytes without decoding them again. Exact encoded-size calculations
and set differences choose the same delta bases and ties without serializing
every candidate. CI logs loading and merge phases. The benchmark-only
`--allow-subset` option cannot be combined with `--check`; ordinary runs still
require the complete matrix. Temporary storage is removed on normal/error exit.

Sorted content-address tables share repeated snapshots, classification maps,
common dependency sets and agreement records. References use 16 hexadecimal
digest digits with collision detection; name maps may use deterministic,
acyclic base-plus-delta records. One table entry occupies one JSON line. The
format and expansion helpers are documented in `ci/library/README.md`; compact
records are reviewable without duplicating library-wide evidence for every
header/build. An approximate profile may use a positive (H, name, value) only
if that literal pair is independent for H in every participating build and all
snapshots agree. Features use the entire major; standard names use the compatible
target/dialect/ordered-argv partition and checked snapshot inputs described above.
Absence requires all possible
definitions reachable from H to be independent; a complete untainted closure
with no definition also proves absence. Unknown, dependent, unavailable or
disagreeing entries remain explicit. A reproduced profile uses its own snapshot,
including build-dependent facts, subject to the native model's uncertainty.

Part B generates a compact, interned table at CMake configuration time and
rejects missing/malformed baseline data. It embeds per-major/scoped agreement and
per-reproduced-profile snapshots for standard-list headers, rather than full
JSON. Reproduced lookup checks the profile's recorded build/command identity;
native-dependent entries remain unknown. Local satisfaction reruns use audited
agreement, since compiler observations alone do not reproduce a named judge's
library build. Reports include generator/schema, the baseline SHA-256 and the
selected profile build, GCC major or standard scope when a resolved decision uses a fact.
Major agreement (including satisfaction reruns) requires a dialect scanned for
that major and one scanned build of that major and dialect containing every
profile/configuration flag outside the explicit harmless
warning/debug/diagnostic/constexpr-limit/pipe allowlist. Flag order does not matter,
and extra build flags are allowed; optimization flags require a scanned match
because they change predefines. This is the legacy generator-3/4 feature-only
coverage rule. Generator 5 requires a compatible captured target triple and
equality of the **ordered** flag sequences
after removing only the harmless allowlist, including the absence of extra
scanned-build flags. Thus an unflagged profile cannot borrow `-m32` or
`-mfpmath=387` values, and a flagged profile cannot borrow a default capture.
Reproduced snapshots still require their complete exact argv and build identity.

Local satisfaction retains the legacy coverage rule specifically for
`__cpp_lib_*` when strict generator-5 selection fails. These values already
require config independence and agreement across every target/argv of the
major. Rejecting them solely because the local target has no exact capture
would demote otherwise valid feature decisions (including Codeforces
`__cpp_lib_to_chars`). Standard integer names retain strict target/ordered-flag
selection; the feature fallback cannot supply their values or absences.

The embedder enforces a 1,000,000-byte generated-source budget. The complete
14-build generator-5 input matrix measured on 2026-11-22 produces a 74,900,608-byte
baseline and 1,654,630 source bytes before integer-only standard-name selection.
Selection retains 503 names (248 feature-test, 255 standard), including
`FLT_EVAL_METHOD`, and reduces the source to 916,768 bytes, leaving about 9%
headroom. The unchanged generator-4 committed baseline emits 588,498 bytes.
These measurements do not import or approve a new production baseline.

**Default library configuration.** Track a separate, monotone Boolean at every
point. It holds iff on every path to that point there has been no user
`#define`/`#undef` of any reserved or standard-macro-list name, macro-stack `push_macro`/`pop_macro`
pragma, system include outside the standard-header list, or `#pragma GCC
target`, `optimize`, `push_options`, `pop_options`. Macro-stack pragmas
continue to disable resolution. At entry, extra user-command reserved or standard-macro-list `-D`/`-U`,
`-include` or `-imacros` beyond the profile's own flags lose the Boolean;
profiles whose own flags have `-include` or `-imacros` get no library facts.
The existing command model rejects these include options; this rule supplies
no new permission to accept them. Joins take logical AND independently of
macro-value joins; losing the Boolean on a path cannot be repaired by a later
matching definition, undefinition or header include. Reserved/standard-list user
writes also revoke already captured header facts immediately: writing a compiler
macro after H can change a header macro that aliases it. Dead paths have no effect.

**Include transition (part B).** A standard-list spelling must resolve to an
actual file inside the compiler's own system include directories. A user file
shadowing `<charconv>` never qualifies, even when marked `-isystem`. While
default library configuration holds, and usable data exists for H:

- Each usable positive fact in the widened universe in snap(H) becomes defined with its
  value. A name proven not to be defined by H keeps its previous state.
- Each unusable, disagreeing or conflicting name becomes unknown. Values
  accumulated from previous headers must agree: different snapshot values for
  the same name make it unknown; a later equal include cannot erase a conflict.
- All other reserved names follow today's invalidation and identity rules.

Otherwise all names in the widened universe become unknown at the include.
GCC option pragmas also invalidate these header facts immediately. These
facts are header transitions, never initial facts or identity exemptions.
A9 asserts the union rule for sequences of qualifying includes; report it when
relied on, alongside the provenance of the snapshots and classification.

**CI and drift.** `library.yml` runs on relevant PR changes and by dispatch,
calls both producer workflows, regenerates the combined data and compares it
byte for byte with the committed baseline. Missing baseline/input, new/removed
name, changed value, condition, dependency, classification or agreement fails
with a unified diff. It uploads both `library-generated/library.json` and
`library-generated/library-evidence.json` even on a comparison failure. Review
both files, then import only the compact `library.json` byte for byte in a
follow-up commit; no baseline is fabricated locally. The weekly drift workflow
runs the full check as the separate `library` claim, with the existing complete
result protocol and per-profile stateful issues. Evidence/availability remain
weekly and source-entry cold reproduction remains monthly. Neither workflow
commits data or advances a profile's human review date.

**Live judge diagnostic.** The `ci/library/probe.py` companion to §4.3.1's
judge probe emits a self-contained TU for a selected standard-list header,
printing the known `__cpp_lib_*` universe's values/absence after that header.
Submit each header as a separate TU to avoid contamination by prior includes.
It adds no diagnostic library includes. `--check` reports missing/extra records,
changed values and header availability differences against that build's
snapshot. Unknown new macro spellings cannot be discovered by a submitted C++
program; CI's complete dump discovers them. A preprocessing-error snapshot has
no successful expectation. This diagnostic uses its own `DLIB` protocol and
does not change the existing DPROBE v1 manifest or shipped expected output.
Like `--judge-check`, live observations never promote facts; part B may expose
this companion through the CLI without changing that restriction.

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
  include outside the standard-header list. Otherwise rejected (R4-F).
  Repeated non-recursive inclusions are inlined again, except that a
  `#pragma once` file can be elided when its generated guard is guaranteed
  defined under A1–A3 and the same name and macro-stack protections. These
  facts use file identity, intersect at conditional joins (including the
  implicit untaken alternative without `#else`), and are invalidated by
  existing system headers outside the standard-header list. Missing angle
  headers cannot change macros in a compiling translation unit. A possibly
  skipped generated guard cannot export new nested include facts. This
  avoids expanding once include DAGs into exponential output trees before
  the resolver can remove dead copies.
- **Line numbers are always preserved** (R4-G): `#line` markers are emitted
  wherever a step changes line numbering (start and end of every splice,
  every resolved directive, every deletion), and the renderer (§4.8)
  maintains them to the final bytes. Every line of the output has the logical
  line it had in its original file, so `__LINE__`, `__builtin_LINE` and
  default arguments that capture the call site behave as before. Cost: one
  `#line` per change point. A marker is spelled `#line N` without the file
  name ([cpp.line] keeps the presumed name) exactly when the previous
  generated marker in textual order names the same file and no directive
  other than `#define`, `#undef` or `#pragma` lies between them; otherwise,
  and always for the first marker, it is `#line N "file"`. Both then lie in
  the same conditional group, so every preprocessing path has that presumed
  name; a marker after a kept `#if`/`#elif`/`#else`/`#endif` or a system
  `#include` always names its file. Each stage re-spells every marker it
  writes, and every reader of generated markers applies the same rule
  (`PresumedFile`), rejecting a short marker without a marker in its segment
  ([reviews/2026-11-18-short-line-markers.md](reviews/2026-11-18-short-line-markers.md)).
- **Opt-in exception** (`--no-line-markers`, user, 2026-11-18): markers
  only change presumed positions of user text, so they are unobservable in a
  program that never reads them. Only the pruning parse (§4.7) can show that,
  so the option applies only to a written `pruned` artifact (else
  `line markers kept: no parse` or `no checked pruned artifact`). The worker
  records every *position use* with a user expansion location: expansions of
  `__LINE__`, `__FILE__`, `__FILE_NAME__`, `__BASE_FILE__`,
  `__INCLUDE_LEVEL__` (also nested and in conditions); final tokens
  `__builtin_LINE`/`FILE`/`FILE_NAME`/`COLUMN`/`source_location`,
  `__PRETTY_FUNCTION__` and MS signature names, `__builtin_dump_struct`;
  call-site default arguments or default member initializers containing a
  source-location builtin (including from system headers:
  `std::source_location::current()`); values, declarations and types of
  `std::source_location`, `std::basic_stacktrace`, `std::stacktrace_entry`,
  `std::contracts::contract_violation`, and uses of `std::meta` or
  `std::contracts`; system macros with a non-standard name invoked from user
  code (their judge definition is unconstrained). A `-Werror…` option or a
  user `#pragma GCC|clang diagnostic error` also keeps them (line-dependent
  warnings would decide compilation). Any one keeps the markers and is
  reported (`line markers kept: __LINE__ expanded at atl/x.hpp:12`). So does
  any function-name expression (`__func__` included), source-location
  builtin, unique-stable-name expression or naming builtin call *anywhere*
  in the parsed translation unit, system or user, template pattern or
  instantiation, or in a default argument evaluated at a call (one
  fail-closed whole-TU visitor; Clang prints `(lambda at file:line)` in
  function names); this loses omission when libstdc++ assertion paths
  (`_GLIBCXX_ASSERTIONS`) are parsed.
  Otherwise the checked `pruned` bytes minus exactly their generated marker
  lines are checked again (§4.8) and written. This is not "markers on
  demand" (§8): a program that can observe positions always keeps them
  ([reviews/2026-11-18-no-line-markers.md](reviews/2026-11-18-no-line-markers.md)).

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

L1c header facts (§4.3.3) are never initial macro facts. Its separate default
library Boolean starts from the command restrictions there.

States reached later from `#define`/`#undef` in the source are *derived*;
they are not compared with any compiler probe (R4-C).

So include guards and library switches the user defines resolve with or
without a profile; `#ifdef LOCAL` (tested, never defined) resolves only with a
profile.

**Transitions:**

The L1c default-library Boolean and feature-macro exception are specified in
§4.3.3 (implemented in part B). The other transitions below remain
unchanged.

- `#define`/`#undef` in an active region set the state.
- An include of a header on the **standard-header list** (C and C++ standard
  headers, `<bits/stdc++.h>`, `<ext/pb_ds/…>`, `<ext/rope>`): every name in
  the *may-define set* becomes *unknown*; user macros and compiler-predefined
  macros keep their state (A2 (a), R4-A). Also (user, 2026-11-18), known
  external facts for covered compiler/target identity names keep their state,
  including known-undefined facts from reproduced-profile absence (A4) or
  the conditional approximate allow-list (§4.3), or explicit profile `-U`.
  The identity registry supplies no initial facts:
  approximate-profile unknowns and no-profile unknowns stay unknown. A source
  definition or undefinition of an identity name loses this protection,
  including the positive compiler-predefine exemption, even after an
  equal-valued join with an external fact. Earlier header/pragma invalidation
  is never undone. The may-define set = every
  reserved name without one of these external exemptions ∪ the macro names the C and
  C++ standards specify (one embedded list, the same one A1 refers to) ∪ the
  extension list for the library (with a profile, its `library`; without, the
  union of all lists). An include never makes a macro *defined*, only
  *unknown* (R3-1), except for the L1c transition in §4.3.3.
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

**Identity registry** (A2(a), user, 2026-11-18) is a separate sorted embedded
table of reserved names and covered libraries. Unlike A1's overapproximation,
an incorrect entry can remove live code; entries need a header audit. The
extension CI scans the same reachable headers for identity `#define`s and `#undef`s,
including inactive branches, writes per-library reports, and fails both scanning
and merging if a covered name is written. Excluded names are reported too.
Computed include operands and unscanned versions remain limitations. This does
not permit arbitrary compiler-looking or library-feature macros to survive.

Satisfaction reruns use the observed configuration's supported target-family
library policy (Linux: glibc/libstdc++; MinGW/Cygwin: MinGW-w64/libstdc++;
Darwin: macOS libc), with the conservative intersection for other targets.
This is the supported-library assumption, not discovery of custom sysroots.
An empty-source probe and a resolver rerun do not independently test A2.
Actual raw/flat preprocessing comparison (§4.6) rejects a header violation that
changes user tokens or their locations in a checking configuration; an
unavailable judge still relies on A2. Local command `-D`/`-U` keep their existing
baseline/raw scope and do not become judge identity facts.

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
`flat` resolution used, each candidate in order is retried with
**demotion** (docs/reviews/2026-11-18-cross-host-demotion.md): the
directives whose true/false decision it contradicts are forced unknown and the
resolver re-runs from raw with the judge's facts, until the candidate
satisfies; a demoted block, including its nested conditionals, stays
verbatim, and each demotion is reported (`flat: demoted <file>:<line>
(#if): local configuration disagrees`). Only if that fails too are `flat`
and `pruned` not candidates and `raw` is written, with the reason in the
report. A build in a non-satisfying
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
are covered by A6 and the never-removed ADL names, member calls by A12 and
D3e's library/language inventory or whole-record fallback, and user specializations
of system templates by rule 3 and the roots. AST edges from system code are
positive evidence only.

**Parse configuration** (D3-1-1). Clang parses in a configuration derived
from the selected `flat` checking configuration (§4.6): its probed triple,
search lists (as §4.2) and dialect, its effective macro operations (the
profile's replacing local `-D`/`-U`, in order, after Clang's predefinitions),
and its semantic options translated to Clang's. Options without a Clang
translation are listed in the report; one without an audited semantics-inert
omission disables pruning. This semantic gate is independent of the profile's
preprocessing-only `inert` permission exemption: that list supplies no proof of
dependency-preserving omission. Modules, OpenMP and CPU options currently have
no supported translation and retain their semantic disables.
`-fconstexpr-depth=N` also remains untranslated. Under A7, the resource/cache
counterexamples in the
[step 2a audit](reviews/2026-11-17-atl-pruning-blockers.md#step-2a-depth-audit-why-translation-remains-disabled)
are excluded, so a resource-only depth translation is sound subject to the
existing error/refusal gates. Translation, numeric validation and pinned-worker
regressions are deferred; this policy change adds no parse-option support. See
[the A7 reassessment](reviews/2026-11-18-constexpr-inert-roots.md#under-a7-2026-11-18).
Without a profile, local checking facts do not become judge facts; unknown judge
conditions or predefined macro values still disable pruning. The report names the parse
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
- a structural record family: non-function members, friends (including hidden
  friend definitions), nested declarations and out-of-line static data definitions;
  D3e function-member units below own their own declarations/definitions. Without
  member pruning the original whole-record family includes functions too. A
  non-inert static data member definition is a root (rule 1), keeping its record;
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

Local declarations belong to their enclosing family, except separately extracted
function-member units of local records under D3e. Namespaces and linkage
specifications (`extern "C" { … }`) are *shells*: they don't root their
contents; an emptied shell goes only if its name no longer appears.

**Directives.** After resolution only conditionals in system headers remain,
so user code has none (pruning precondition). Source directives in user code
that remain (`#define`, `#undef`, `#pragma`, system `#include`) are never
removed; a family whose range crosses one is pinned (a function containing a
`#define` stays whole). Generated `#line` markers (D2 provenance, not their
spelling) are rendering metadata, not directives: they don't pin, and §4.8
recomputes them (D3-1-8).

Never removed: macro definitions, non-function members of a kept record
(function members use the D3e member-unit rules below), declarations produced by a macro
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
concept and namespace-alias names, and unscoped enumerators. D3e also indexes
function-member names in their own units; template parameter names and other
local/member names are not indexed.

**Member units (policy D3e, A12).** Functions and function templates with one
name in one record form a unit, including overloads, in-class declarations,
out-of-line definitions and specializations. Nested records have separate units.
Data/static data members, nested types, aliases, enums and friends stay with the
structural record family. A kept record no longer automatically keeps all methods.
A unit stays if any of these eight rules applies:

1. Constructors, destructors, conversions, operators, deduction guides, or any
   virtual member (including virtualness in known instantiations) are excluded.
2. Any user virtual member anywhere has the same name.
3. The name is reserved.
4. The name is an identifier token in the raw text of any included system header
   or any reachable header of a scanned library build (including libc),
   including transitive includes, inactive branches and macro definitions, or is
   produced by macro expansion of those headers in the full unpruned TU, or is
   a language lookup name (`begin`, `end`, `get`, coroutine protocol names).
5. The name is an identifier token in kept user text outside its own unit,
   including other units, macros and structural text.
6. Kept AST code references the member, or a kept template pattern has a matching
   dependent member/scope-reference name; this includes user-only pasted names.
7. It has a root/pin: non-inert local static/thread storage, interpreted
   attribute/assembly, keep annotation/flag, source directive or macro invocation.
8. Its record or class template is explicitly instantiated (declaration or definition).

Rule 4 uses the worker's include graph after parsing, then raw lexing with Clang
identifier normalization, excluding comments/string contents. It unions this with
identifiers from the full unpruned TU's preprocessor token stream whose expansion
originates in a system macro definition or system context, including pasted names
and user-supplied macro arguments. The worker unions the embedded global sorted
identifier inventory from every eligible L1c build (§4.3.3). Scanned token pasting
is report-only; actual worker expansion supplies pasted names for this TU.
This widening only keeps more members. Under A12 these inventories bound the
judge's; unexpanded worker macros produce no names in this TU. An unavailable
worker inventory disables member pruning with a reason. The existing generator-3
baseline has no scanned inventory: member pruning uses worker headers only and
reports `scanned-build inventory unavailable` until the CI data import.
Whole-family pruning continues under its existing gates.
`--no-assume=library-spelled-members` likewise keeps records whole.

Member units join the ordinary fixpoint and rule 6's reverse potential dependency
graph below as families. Forward member-to-owner retention keeps records; owner-to-blocked-member
edges model rules 1–4/8. Name-only matches wait for an owner to survive, so unrelated
spelling cannot resurrect a dead record. Roots, AST references and effect triggers
may retain it. Units statically retained with their owner (rules 1–4/8, including
every declaration of an aggregated unit) share the owner's effect component and
incoming system-dispatch analysis. D3d-7 already accounted for those members in
the whole record; splitting their edit/report rows adds no new library hook.
The synthetic ownership relationship is forward retention only for other units;
actual AST, lexical and dependent-name references remain potential effect edges.
Free hooks, uncertified hidden friends and system specializations still use the
existing unbounded-system-instantiation refusal. Missing inventories select whole-record
extraction and its existing effect rules. A11 and its opt-out apply to units too.

Edits own template headers, attributes, default arguments and terminators, leaving
access labels and record braces intact. Mixed function/data declarations are
pinned. Reports give one row per member unit and kept/removed counts. A12 is
reported whenever member pruning is enabled, even without a deletion, like A11.
See the [adopted member review](reviews/2026-11-20-member-pruning.md) for the
critical review and historical prototype measurements.

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
   `std::ptrdiff_t` or a standard C/C++ integer/size typedef. Imported scalar
   typedefs represented by `UsingType` are followed too; written type-qualified
   member selections remain conservative even when locally scalar.
   Policy **D3d-3**, under A7, additionally exempts defining `constexpr`
   variables with static storage, no thread storage, volatile/reference type or
   variable attributes, when `safeSystemMacros` holds and either:
   (a) their expanded, as-written type is builtin arithmetic, ignoring cv
   qualification only, even with a value-dependent initializer; or
   (b) their written type directly names a complete, named, non-local,
   non-template, non-dependent user class, not a union or specialization, with
   no bases, non-static data members, user-declared special members, virtual
   functions or attributes, a trivial destructor and an empty list initializer
   (`{}` or `= {}`). Classes nested in templates/specializations are excluded.
   This added exemption rejects aliases, `auto`, `decltype`, parameter-typed
   members, pointers, arrays and enums; the original whitelist still applies
   independently. Implicit instantiations use the pattern's written type;
   explicit specializations are classified from their own declarations.
   Neither exemption erases attribute roots or skips ordinary dependency
   traversal. Everything else remains a root, including `constinit` or thread
   storage outside the original whitelist. See the
   [soundness review under A7](reviews/2026-11-18-constexpr-inert-roots.md#under-a7-2026-11-18).
   A function-local variable without static storage is never a root by itself.
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
6. **Instantiation effects** (policy **D3d-7**): preserving a template's
   definition does not preserve the code that instantiates it. Effect sources
   are static/thread storage definitions in template contexts outside rule 1's
   inert exemptions and the additional storage proofs below, interpreted
   attribute roots in template contexts, and
   template entities referenced by interpreted assembly/alias symbols
   (retaining their patterns alone need not emit the referenced specializations).
   Rule 1's roots are unchanged. Under A11, constexpr variables of any scope
   or type and all block-scope static/thread variables are not storage effect
   sources. Constant initialization has no independent runtime effect;
   dynamic initialization of block storage requires control to reach its
   declaration, preserved through ordinary dependencies. A11 excludes constexpr
   destruction (including lifetime-extended temporaries) and destruction of
   block storage whose declaration is never reached. This block exemption is
   an assumption, not a language guarantee inferred from compiler behavior.
   Other static/thread storage is exempt only with an all-target proof of
   constant initialization, no possible non-trivial destruction of its own
   type, and **no lifetime-extended temporary in its initializer**, including
   through aggregate reference members and default member initializers.
   The additional proof uses the existing syntactic constant initializer proof
   and a written, target-stable non-reference scalar/array type or a provably
   trivially destructible user class. User aliases are inspected as written;
   classes must be non-template and all their bases/fields must have provably
   trivial destruction. References are allowed as fields only; their
   initializers still must pass the syntactic proof, which rejects materialized
   temporaries and default member/default argument expressions outright.
   Dependent types/initializers, deduced types, template-selected member types
   and system class selections do not pass this additional proof. Implicit
   specializations use their pattern; explicit specializations are checked
   independently. The original positive inert exemptions remain in force.
   This includes member and nested templates, partial specializations,
   variable and function templates,
   generic lambdas, and out-of-line members. Keep effect sources and every
   family that can reach one in the potential dependency graph of **all**
   families, including otherwise unused declarations and template patterns.
   Edges are identifier-token references to indexed names, AST references,
   dependent-call name matches, type associations, family unions and positive
   system-entity edges. Reverse transitive closure covers aliases, default
   arguments, unevaluated operands, constraints, constant evaluation and
   default member initializers; ordinary forward closure still runs afterward.
   Report these as `instantiation effect: ...`, with the effect source as cause.
   Clang's observed instantiations are positive evidence, never a filter on
   possible triggers. If the reverse closure reaches a user specialization of
   a system template or a free ADL/operator hook, pruning is disabled with
   `unbounded system instantiation effects through ...`: A6 bounds hook names,
   not their incoming library instantiations on another target. Hidden friends
   in record families instead have bounded carrier/association paths only when
   every non-member callable in the family has a direct parameter carrying that
   same record family (optionally through pointers/references; aliases and type
   substitutions do not certify this). This certificate remains necessary when
   a hidden hook reaches a remaining storage or attribute effect, even though
   the friend declaration itself is no longer an effect source. The graph has a
   four-million-edge limit; exceeding it disables pruning. See the
   [instantiation-effects review](reviews/2026-11-20-instantiation-effects.md).

**Under A11 (user, 2026-11-20).** Function friend declarations in templates,
potential class-valued non-type template parameter objects, constexpr variables
and block-scope static/thread variables no longer seed rule 6. Their ordinary
name, type, value and AST dependencies still participate in forward closure.
Non-constexpr static/thread storage outside blocks can still introduce dynamic
initialization or destruction independently of control reaching a declaration;
it remains a source unless positively proven harmless as above. Templated
attribute/symbol roots also remain sources. Reverse closure preserves their
potential triggers, including dormant patterns and target-dependent paths;
unbounded incoming library hooks still refuse. Every enabled pruned artifact
reports `A11 instantiation-effects` alongside A7, even when no family is deleted
(unless disabled by the flag below).

**Without `instantiation-effects` (user, 2026-11-20).**
`--no-assume=instantiation-effects` keeps policy D3e and pruning enabled but
restores D3d-5's function-friend declarations in template contexts and potential
template parameter objects as rule-6 effect sources. The pre-A11 NTTP proofs
remain valid: written scalar/enum/pointer/reference/member-pointer types,
recognized scalar aliases and recursively proven trivial non-template user
classes do not introduce parameter-object destruction. Unknown placeholders,
dependent bindings, system class selections and template-qualified member
types remain conservative, including locally scalar or trivial selections.
All non-inert template static/thread storage, including constexpr and block
storage, is again a source unless the constant-initialization, trivial-type and
no-extended-temporary proof above succeeds. Necessarily-dynamic block locals
are conservatively retained as sources; the optional dynamic-initialization
proof is not used. Rule-1 exemptions, forward/reverse closure, attribute/symbol
sources and refusal gates still apply. This mode needs A1–A7 and A9 where
applicable, plus A12 when member pruning is enabled, but does not need A11. Reports say
`policy: D3e, without instantiation-effects`, explicitly record that the flag
disabled `A11 instantiation-effects`, and omit it from relied-on assumptions.
The setting is part of serialized worker protocol version 7 and checked as
immutable input on return. There is no pruning-result cache; the serialized
model and policy identity distinguish both A11 and A12 flag combinations.

**Static invariant:** rule 2 on the final rendered text: no removed family's
indexed name appears as an identifier token. For member units this additional
check applies to individually removed units of kept owners; members removed
with a whole record may share names with retained records. It catches range and editing
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
  `M::op`, `T::value`, `typename Tr::type`) don't disable pruning. D3e's
  lexical/AST/dependent member-name rules preserve function units, including
  out-of-line definitions and specializations; non-function members remain
  with their record. The record or enum carrying lookup is reached through
  type carriers (template arguments spelled in kept text, AST edges of seen
  instantiations, return and alias chains, kept bases). With member pruning
  disabled, the prior whole-record closure applies. A system trait's selection
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
- **`no_unique_address` (policy D3d-2):** only the standard, argument-free
  `[[no_unique_address]]` spelling is supported, on a non-static, non-bit-field
  `FieldDecl` with verified ownership by its enclosing record. Both empty and
  nonempty member types are allowed, including template and nested records and
  macro-produced attributes. D3e preserves the structural record, all data
  members and excluded special members; the attribute does not root an unused
  record. Ordinary traversal still retains dependencies of member types,
  initializers, constructors and destructors. Expanded tokens and the parsed
  attribute class/context must agree: discarded attributes, arguments (even
  `()`), other declaration/statement contexts, `msvc::no_unique_address` and
  other scoped/vendor spellings disable pruning with `unsupported attribute`.
  The pinned frontend headers/AttrDocs are absent from this source tree, so the
  unaudited `__no_unique_address__` spelling remains unsupported. Safety follows
  from preserving layout-relevant declarations and their dependency closure,
  without assuming equal Clang/GCC layouts or selecting a particular
  `sizeof`/trait branch. Removable nonvirtual function members cannot affect layout.
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
  strings, in the short form `#line N` where §4.4 allows it.
- **Rendering invariant** (D3-1-11): the output is lexed again; every
  surviving token must have its intended logical file and line, and the
  retained source directives must keep their token sequence and order.
  Failure rejects the candidate.
- **Marker-free output** (`--no-line-markers`, §4.4): derived only from a
  `pruned` artifact that passed every check, by deleting exactly the
  renderer's generated marker lines (each a whole `#line` line). An
  independent re-lex must find the same tokens, kinds and directive
  boundaries as the marked output minus its `#line` lines and no line
  directive; `-E` of both in the `flat` configuration must give the same
  tokens and `#pragma` boundaries, with tokens of the same presumed file
  (system headers) on the same line; the result builds and its runs equal
  `flat`'s. A failure keeps the marked `pruned` bytes. The rendering
  invariant with line provenance applies to the marked output; the proof
  that positions are unobserved replaces it for the marker-free one.
- **Compact whitespace** (`--compact-whitespace`, opt-in, user 2026-11-22):
  implies `--no-line-markers`; only the checked marker-free `pruned` artifact
  is eligible. The §4.4 proof already covers column observations through
  `__builtin_COLUMN`, whole-TU source-location expressions (including system
  defaults), and `std::source_location` values/types/member calls such as
  `column()`. No extra assumption is made. Remove leading horizontal whitespace,
  trailing whitespace and empty physical lines, except inside literals/comments
  and whole logical lines containing a backslash-newline (including CRLF/GCC
  whitespace splices), whose bytes stay intact. Keep nonempty lines separate,
  preserving directive start/end boundaries and `#` line-start semantics.
  Re-lex and require equal tokens/kinds/directive boundaries; compare `-E`
  tokens/pragma boundaries with the marker-free artifact (also verifying
  stringized multi-line macro arguments); build and compare samples on the
  exact compact bytes. A failed compact check retains the checked unmarked
  bytes. A failed gate or marker-free check leaves its artifact unchanged.
  Report `whitespace compacted: ...` or `whitespace kept: <why>`. Renderer
  annotations/provenance are consumed before this final transformation.
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
date, option classes, the assumptions (A1–A7, A11, A12 for member pruning, and A9 for L1c) the output relied on. Then one
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
about the artifact written. With `--no-line-markers` the report (and
stderr) has `line markers omitted: …` and a `pruned without line markers`
check, or one `line markers kept: <reason>` line per reason (at most ten).

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
conditionals left in user code; line markers only on demand (omitting
them is opt-in and only when the parse proves no position is observed, §4.4).

## 9. Open questions

None for the user (Q1–Q5 answered 2026-11-15). D1 may raise more (official archives vs own builds); D3's measurements decide
whether the name rule and the never-removed rules need refining.
