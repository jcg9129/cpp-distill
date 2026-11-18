<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Opt-in `--no-line-markers` (2026-11-18)

## Verdict

**Sound as an opt-in for the `pruned` artifact only; implemented.** With
`--no-line-markers`, distill writes the checked `pruned` bytes with every
generated `#line` marker line removed, but only when the Clang parse of
`flat.cpp` that pruning already performs finds no construct in user code
whose behaviour can depend on presumed source positions, and only after the
marker-free bytes pass their own checks. Otherwise the markers stay and the
report says why (`line markers kept: __LINE__ expanded at atl/x.hpp:12`).
Default behaviour (R4-G, markers always) is unchanged. No counterexample
without a fix was found; the residual reliance is stated under
"Assumptions".

The design (§8) rejected "line markers only on demand" because omitting them
changes `__LINE__`, `__builtin_LINE` and call-site defaults. This option keeps
that rejection for every program that can observe positions: it is not
"on demand", it is "on demand, when proved unobservable".

## What a generated marker changes

[cpp.line]/3–4: `#line N` and `#line N "F"` change only the *presumed* line
and file of the following source lines. GCC (`do_line`, `LC_RENAME_VERBATIM`)
and Clang (`AddLineNote` without entry/exit flags) keep the file's
system-header flag, its include depth and its physical identity, so
`#include "…"`/`__has_include` lookup (physical directory), `#pragma once`
(physical file), system-header classification and `__INCLUDE_LEVEL__` are
unaffected. Location ordering used for `#pragma GCC diagnostic`/`optimize`
regions follows the token stream, not presumed numbers. Markers are whole
lines inserted at logical line starts outside directives, comments and raw
strings (§4.8), so removing them changes no token, no token boundary and no
directive boundary (checked independently, below).

Markers appear only in user text. A kept system `#include` starts a new
presumed file at line 1 and restores the includer's state on return; so
system header text has the same presumed positions with or without markers.
The only presumed fact a system header could read about its includer is
Clang's `__BASE_FILE__` (Clang walks the include stack to the base file's
*presumed* name; GCC uses the main input name). No header of glibc,
libstdc++ 15/16 or libc++ on the audit host uses `__BASE_FILE__`,
`__INCLUDE_LEVEL__` or `__FILE_NAME__` (`grep -r /usr/include`); expansions of
system macros *in user code* are covered below.

Therefore the marker-free artifact `U` and the marked artifact `P` are the
same token sequence; a program that does not observe presumed positions of
user tokens has identical semantics for `U` and `P`. `P` is the checked
`pruned` artifact, so `U` behaves like `flat`.

## Observation channels and how each is excluded

The parse sees the checked `flat.cpp` (user conditionals all decided:
pruning requires no unknown conditional in user code, R4-H) in the selected
judge-equivalent configuration. Everything below is recorded by the worker as
a *position use* with its user offset; any one keeps the markers. Detection
covers all of `flat.cpp`'s user code, a superset of what `pruned` keeps.

1. **Position macros.** Every expansion with a user expansion location of
   `__LINE__`, `__FILE__`, `__FILE_NAME__`, `__BASE_FILE__` or
   `__INCLUDE_LEVEL__` (the last two are already rejected in user code,
   §1.2), including expansions nested in other macros (`assert` without
   `NDEBUG`: MinGW's, macOS's and older glibc's replacements contain
   `__FILE__`, `__LINE__`) and inside
   `#if`/`#elif` conditions. Clang calls `MacroExpands` for builtin macros
   before expanding them, so nested and rescanned (`##`-synthesized)
   occurrences are seen. `__COUNTER__` is not position-dependent and disables
   resolution anyway; `__DATE__`/`__TIME__`/`__TIMESTAMP__` don't depend on
   markers (and are not behaviour, §1.1).
2. **Position builtins and position-printing keywords**, by final token in
   user code (spelled or produced by any macro): `__builtin_LINE`,
   `__builtin_FILE`, `__builtin_FILE_NAME`, `__builtin_COLUMN`,
   `__builtin_source_location`, `__builtin_FUNCSIG`, `__PRETTY_FUNCTION__`,
   `__FUNCSIG__`, `__FUNCDNAME__`, `L__FUNCSIG__`, `__builtin_dump_struct`.
   This catches current glibc's C++ `assert`, which spells `__builtin_FILE ()`
   and `__builtin_LINE ()` instead of the macros when `__has_builtin` says so
   (so the parse and a GCC judge may see different spellings of the same
   observation; both are caught). Rule 3 subsumes this for parsed code; the
   token rule stays as a second line.
3. **Function names and source positions anywhere** (after three Codex
   reviews). Clang prints closures and unnamed types (and values of unnamed
   enums) with their presumed location, e.g. `(lambda at main.cpp:5:12)`, in
   `__PRETTY_FUNCTION__`, `__func__` of a conversion operator to an unnamed
   enum, `__builtin_dump_struct` output and source-location function names;
   a call-site default argument evaluates `SourceLocExpr` at the caller.
   Hand-written partial walks (user code only, then instantiations "involving
   user types", then local classes one level deep) each missed a route:
   nested `Outer<closure>::Inner`, an integral argument of an unnamed enum,
   `__builtin_dump_struct`, a depth limit answering "no", a conversion
   operator outside any template, a local class nested in a local class. The
   rule is therefore **one fail-closed `RecursiveASTVisitor` over the whole
   translation unit** (`shouldVisitTemplateInstantiations`,
   `shouldVisitImplicitCode`; patterns and instantiations; lambdas; local
   classes at any depth; system and user code) that records ANY
   `PredefinedExpr` kind (`__func__` included), any `SourceLocExpr`
   (`__builtin_LINE`/`FILE`/`FILE_NAME`/`FUNCTION`/`FUNCSIG`/`COLUMN`/
   `source_location`), any `SYCLUniqueStableNameExpr`, and any call to a
   builtin whose name contains `dump`, `name`, `FUNC`, `FILE`, `LINE`,
   `COLUMN`, `source_location` or `sycl` (over-inclusive by design). There is
   no filtering by location, user-ness or template arguments. Every
   `CXXDefaultArgExpr`/`CXXDefaultInitExpr` has its expression (and the
   parameter's default or field's initializer) traversed explicitly, whether
   or not this RAV version descends into it. The single exclusion: a
   parameter's default argument *as a declaration* is not traversed, because
   it is evaluated only through a `CXXDefaultArgExpr` at a call, which is;
   so an unused `std::source_location::current()` (or `std::meta::exception`)
   declaration in `<bits/stdc++.h>` doesn't count, while any call using it
   does. Exceeding 50 million visited statements stops the scan and counts as
   a use. The first occurrences are reported at their location in user code,
   or as `(outside user code: <file:line:col>)`.

   **Cost:** omission is lost whenever any such construct exists anywhere in
   the parsed headers or user code, even unrelated to user types. In
   particular libstdc++'s assertion paths (`__glibcxx_assert` →
   `__PRETTY_FUNCTION__` under `_GLIBCXX_ASSERTIONS`, which GCC 15+
   libstdc++ enables by default for unoptimized builds, and `_GLIBCXX_DEBUG`)
   keep the markers whenever they are expanded in any parsed function, used
   or not, although that text only reaches stderr. Typical judge builds use
   `-O2`; whether the remaining `<bits/stdc++.h>` text contains such a
   construct at `-O2` is to be measured in CI (if it does, the option never
   omits with that header).
4. **Position-carrying values.** Any expression, declaration or written type
   in user code whose type (through references, pointers, arrays, function
   returns and typedef sugar) is `std::source_location` (or its `__impl`),
   `std::basic_stacktrace`, `std::stacktrace_entry` (debug-info lines are
   presumed lines) or `std::contracts::contract_violation`, and any reference
   to a declaration in `std::meta` or `std::contracts` (reflection's
   `source_location_of` and display strings; a user violation handler).
   `bits/stdc++.h` includes `<stacktrace>`, `<meta>` and `<contracts>` in
   GCC 16, so inclusion alone does not block; use does.
5. **System macros whose judge definition is unconstrained.** A system-header
   macro invoked from user code (its name token is not spelled inside a
   system header, so pasted names count) whose name is not on the standard
   macro list (§4.5) keeps the markers: the judge's headers may define it
   differently from the parse's (`M_PI`, `__glibcxx_assert`). Standard macros
   are accepted: their effect is specified independently of the source
   position except `assert`'s diagnostic, which goes to stderr (§1.1) and is
   caught by rule 1 whenever the parse configuration enables it.
   Predefined-macro expansions in user code already agree between parse and
   judge or pruning is disabled (D3d-4); command operations are judge facts
   (A4/A5).
6. **Diagnostics as behaviour.** Diagnostics are not behaviour, but a warning
   whose emission depends on line layout (`-Wmisleading-indentation`) would
   change compilation success under `-Werror`. A `-Werror…` option in the
   checked command or a user `#pragma GCC|clang diagnostic error` keeps the
   markers. `static_assert`/`#error` text and warnings otherwise are
   unaffected behaviour.

Not channels: `typeid(T).name()` (Itanium mangling numbers closure and
unnamed types by order, not position; MSVC's ABI is not a target),
`__builtin_FUNCTION`, `__func__`, unknown judge conditionals (none can remain
in user code of a `pruned` artifact), conditionals in system headers
(unaffected text). A program reading its own executable or debug info by
other means already sees a different binary after pruning and is outside
the §1.1 contract.

## Where the information is available

Only the pruning parse has a Clang view of user code. The option therefore
applies only when the written artifact is `pruned`:

- `--no-prune`, or a parse that never ran: `line markers kept: no parse`;
- parse ran but `pruned` was rejected or disabled:
  `line markers kept: no checked pruned artifact`.

## Checks of the marker-free artifact

After `pruned` passes all its checks (rendering invariant with line
provenance, static name invariant, build, samples), distill derives `U` by
deleting exactly the renderer's generated marker spans (each must start a
line, end with a newline and be a `#line` directive) and then:

1. **Lexical invariant** (independent re-lex of both texts): `P` without its
   `#line` lines and `U` have the same token sequence, token kinds and
   directive boundaries; `U` contains no `#line` or linemarker directive.
2. **Preprocess comparison** in the selected `flat` configuration: `-E` of `P`
   and of `U` give the same tokens and `#pragma` boundaries; a token whose
   presumed file is the same in both (system headers) must also keep its
   line, so markers provably did not renumber system text.
3. **Build and runs** in the same configuration; samples equal `flat`'s.
4. The proof obligation above (no position use, no `-Werror`).

Any failure keeps the checked marked `pruned` and reports
`line markers kept: marker-free check failed: …`. Verification level and
the result line are those of `pruned`.

## Assumptions

No new trust assumption. Rule 5 relies on the judge's standard library
giving standard macros their specified effect (the same conformance every
artifact relies on for the library's behaviour); A1/A2 already bound what the
headers define. The rule 3/4 lists are complete for libstdc++ 16; a future
library adding a standard API with a defaulted `source_location` parameter is
covered by rule 3 whenever the parse's library has it, and otherwise the
call doesn't parse and pruning is disabled.

## Tests

- core (local): omission with the synthetic worker; kept for injected
  position uses (`__LINE__`, default argument); kept for `-Werror` and a
  diagnostic-error pragma; `no parse` under `--no-prune`; default unchanged;
  marker removal and the lexical invariant reject a glued or lost token.
- worker (CI, LLVM 23): `__LINE__`, `assert` without `NDEBUG` (and omitted
  with it), a user function with a defaulted `__builtin_LINE()` parameter,
  `source_location::current()` from a system header, a stacktrace-typed
  value, a non-standard system macro; a test-only `-isystem` header's
  `signature(T)` returning `__PRETTY_FUNCTION__` called with a lambda, a
  `__func__` member of a class template specialized with a user type, an
  instantiation with `int` only (kept: argument-blind), `Outer<closure>::
  Inner`, an integral argument of an unnamed enum, `__builtin_dump_struct`
  in a system template, a 20-deep nested argument, a local class nested in a
  local class of a system template, `__func__` in a user conversion operator
  to an unnamed enum, `__func__` and `__builtin_FUNCTION()` in user code
  (kept), and a plain program whose header declares an unused defaulted
  `__builtin_LINE()` parameter (omitted). Each header case writes its own
  header, since any construct anywhere keeps the markers.
- pipeline (CI worker/CLI): omitted for a plain program; kept for `__LINE__`;
  `source_location::current` in user code stays rejected by the inliner.
