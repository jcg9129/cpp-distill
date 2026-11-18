<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Boolean precision for conditions with unknown operands (2026-11-18)

## Verdict

**The general rule "`false && X` = false, `true || X` = true for an unknown
operand X" is unsound for this preprocessor model** because macro replacement
is textual: an unknown macro can change the parse of the whole expression.
**A narrower rule is exact and is implemented:** an unknown macro that occurs
only as the operand of `defined` contributes exactly one of the integers 0/1,
so the condition is evaluated once per definedness assignment of those names
(at most 2^4) and decided only if every variant evaluates without error to the
same value. This covers the motivating atl-cpp case and every short-circuit,
ternary and nested-arithmetic shape whose unknown parts are `defined` tests.

## How unknowns propagate today

`resolve.cpp` has no three-valued evaluator. `Engine::condition` walks the
directive tokens (and the replacement lists of known macros) to collect the
macro closure; the first unknown name, unknown definition or unknown builtin
invocation sets `reason` and the directive is decided unknown before anything
is evaluated. Otherwise a self-contained source (closure definitions plus the
`#if`) goes to the `ConditionEvaluator`, a real preprocessor (GCC `-undef -E`
in tests, the bounded Clang worker in the CLI). So `&&`, `||`, `!`, `?:`,
comparisons and arithmetic are all handled by the real preprocessor, but only
when no operand at all is unknown. The hypothesis is right in effect: in

    #if defined(ATL_USE_MMAP) && defined(__has_include)

`ATL_USE_MMAP` is undefined (A5) and `__has_include` is unknown in approximate
profiles (special builtin fact not reproduced), so the directive becomes
"unknown macro: __has_include" although the result is 0 for every judge.

## Why token-level short-circuiting is unsound

Splitting the unexpanded directive at a top-level `&&`/`||`/`?:` and deciding
from the known side assumes the unknown side is a self-contained operand. A
value use of an unknown macro is not: with `#define X 0 || 1` on the judge,

    #if 0 && X      ->   0 && 0 || 1   ->   (0 && 0) || 1   = 1

and an unknown function-like macro or unbalanced replacement can capture the
operator or a parenthesis. The same applies to known macros whose replacement
is not a parenthesised primary, and to `__has_include(...)` style invocations
of an unknown name (a user macro of that name can expand to anything). No
splitting of the unexpanded token sequence is therefore done.

## Why enumerating `defined` operands is exact

[cpp.cond]: `defined X` / `defined ( X )` is replaced by `1` or `0` *before*
macro replacement of the controlling expression, and its value depends only
on whether X is a macro name at that point, not on X's replacement list. The
resolver rejects macro-generated `defined` ("macro-generated defined"), so
every `defined` it sees is in the directive itself. Hence for a name U whose
state is unknown and which is used only under `defined`, the judge's
condition is one of the two token sequences obtained with U defined or
undefined; everything else in the expression (structure, other macros,
literals) is identical. Evaluating every combination with the real
preprocessor and requiring identical results is an exact decision procedure
for the judge's value under every completion of the unknown facts. No
reasoning about `&&`, `||`, `?:`, signedness or usual arithmetic conversions
is done by the resolver: the evaluator applies intmax_t/uintmax_t semantics,
the int type of `&&`/`||`/`!`/`defined`, and the converted type of `?:`
itself, so nested uses such as `(defined(A) && defined(U)) + 1 > 1` or
`defined(U) ? 1u : -1` are exact.

This needs `defined` to be evaluated by the `#if` itself. Inside the argument
of a function-like macro (`#define F(x) x`, `#if F(defined(U)) || 1`) the
`defined` is macro-generated (undefined behaviour in the standard) and
compilers may macro-expand U in the argument first, so the result depends on
U's replacement. Hence no name is enumerated when the closure contains any
function-like macro (directly or through an object-like alias); the directive
stays "unknown macro" as before.

A name used both under `defined` and by value (directly or through a known
macro's replacement) is still a value use and keeps the old behaviour
("unknown macro"). Unknown builtin invocations (`__has_include(...)` with an
unknown binding) keep "builtin invocation". The variant sources define a name
as `#define U 1`, as the existing `definedUnknown` path already does; GCC and
Clang accept this for special names such as `__has_include` after the
isolation `#undef`.

## Errors in the unknown operand

- `defined U` is well formed for every identifier U whatever U's state, so the
  judge cannot reject the unknown operand itself; definedness does not change
  the token structure.
- A malformed expression (missing identifier after `defined`, unbalanced
  parentheses, stray tokens) is malformed in every variant, so every variant
  fails, the directive stays unknown and is kept verbatim, and the judge sees
  the same error as before. Nothing is hidden.
- Errors that depend on values (division or remainder by zero in an evaluated
  operand, e.g. `1 / defined(U)`) occur in some variant; the rule requires
  *every* variant to succeed, so such a directive stays unknown and the judge
  still evaluates (and possibly rejects) it. A division by zero in an
  unevaluated operand of `&&`/`||`/`?:` is not an error ([expr.log.and],
  [expr.cond] evaluation rules apply in `#if`); GCC and Clang agree, and
  where they did not, the variant would fail and the directive would stay
  unknown, which is conservative.
- Warnings (e.g. `defined(__VA_ARGS__)`) are identical across variants since
  the real token is kept (no renaming).

The raw program compiling on the local configuration is not relied on for any
of this.

## Requirements, provenance and assumptions

- `requirements.integers`/`dialect` are computed from all tokens of the
  expression and the closure exactly as before, so a decision records at least
  the requirements it could depend on (over-approximation, unchanged policy).
- `external` (which drives the A3/A4/A5 report and `fact` lines) keeps every
  known/defined-unknown closure fact with external provenance, including the
  known operand that decides the result (`ATL_USE_MMAP`, A5). It omits the
  enumerated unknown names: the decision is invariant under every value of
  them, so it makes no claim about them, and listing them would add a spurious
  A4 ("profile facts") assumption. Facts of a known operand that happened to
  be short-circuited stay recorded, as they already were for `#if 0 &&
  defined(K)`; that is conservative.
- Enumeration happens only after the closure walk has succeeded, under the
  same deadline; each variant is a full evaluator call. The count is capped
  at 4 names (16 calls); beyond that the directive stays "unknown macro".
  A directive that is a single, possibly negated `defined U` (every
  `#ifdef`/`#ifndef` of an unknown name) obviously depends on U and stays
  unknown without any evaluator call, so the common case costs nothing extra.
  When variants disagree the reason stays "unknown macro: U"; when a variant
  fails it is "evaluator error: ...".

## Tests and an observation

The atl-cpp shape reproduces with the shipped `cses-gcc13-cpp23` profile
after a standard include (`tests/cases/resolve/defined_short_circuit`); the
other shipped profiles record `__has_include` as a defined special builtin,
so `defined(__has_include)` is already true there. Separately, at the start
of a file (before any standard include) `cses-gcc13-cpp23` gives
`__has_include` the state *undefined* with provenance allowList, because its
snapshot has no `# state __has_include` line and `initialState` falls back to
"allowlisted reserved name absent from the snapshot". GCC 13 defines
`__has_include`, so that fact looks wrong; it is outside this change and is
reported for follow-up.

## `satisfies` and cross-host demotion

A decision made by enumeration is the same for every completion of the unknown
facts. Therefore (a) a local configuration whose known facts agree reproduces
it, so `satisfies`/`sameDecisions` behave as for any other decision, and (b)
the rule is monotone: giving the resolver less information (demotion turns
facts into `join` unknowns, never adds facts) can only turn decisions into
unknown, never flip them. Forced directives skip `condition` entirely, so
`forcedUnknown` and the verbatim rule of
`2026-11-18-cross-host-demotion.md` are unaffected, and the forced set still
grows strictly. A directive after a demoted block that only tests the joined
name under `defined` may now be decided if the result is invariant, which is
exactly the soundness argument of that review (every fact used is known;
unknown ones provably do not matter).

## Not done

- Short-circuiting around value uses of unknown macros (unsound, above).
- Algebraic rules such as `0 * X` (same textual-expansion problem; and the
  enumeration already covers the `defined` cases exactly).
