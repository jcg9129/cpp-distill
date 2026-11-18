<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Cross-host demotion of disagreeing conditionals (2026-11-18)

## Verdict

**Sound, at directive granularity, re-running the full resolver with a set of
forced-unknown directives, and keeping each demoted block verbatim including
its nested conditionals.** Demoting by macro fact is not adequate (see
"Granularity"). Patching decisions in place is unsound (see "Dependencies").
Phase 2 proceeds with this variant.

## Problem

Since the A2 identity registry (#90), approximate x86_64 profiles give definite
facts for `__x86_64__`, `__aarch64__`, ... A library block such as
`#if defined(__i386__) || defined(__x86_64__) || ...` resolves true for the
judge. On an aarch64 host (this VM, Apple Silicon) the x86 check compilers are
skipped as triple-incompatible and the local rerun decides the block false.
`sameDecisions` fails, no configuration satisfies, and the output degrades to
`raw` (and pruning becomes impossible). Before #90 the condition was unknown,
kept verbatim, and `flat` succeeded.

## Scheme

1. Pass 1 (unchanged): try every candidate configuration without demotion.
   The first one that satisfies is authoritative. So whenever a configuration
   agrees, the output is byte-identical to today's.
2. Pass 2, only if nothing satisfied: for each probed, triple-compatible
   candidate in the same order, take its observed rerun `b` and the judge
   resolution `a`. Let `D(a, b)` be the raw lines of every true/false entry of
   `a` that `b` neither reproduces nor proves dead (exactly the entries that
   make `sameDecisions` fail). Add `D` to a forced set `F` and re-run the full
   resolver with the judge facts and `F`. Repeat until `sameDecisions(a', b)`.
3. A forced `#if`/`#elif` is decided unknown ("demoted") instead of evaluated;
   later alternatives become "prior unknown condition" as for any unknown
   condition. A forced `#else`/`#endif` (possible only as a consequence of a
   group-level disagreement) forces the first alternative of its group.
   Everything nested inside a demoted block is also decided unknown ("inside
   demoted conditional").
4. Stop with the old "no configuration satisfies" outcome when the rerun is
   disabled, the inventories differ, `D` adds nothing new, or the deadline of
   the selection step expires. `F` grows strictly, so at most one iteration
   per conditional directive.

## Why keeping a directive unknown is sound

`flat` is correct for the judge if every removed alternative is one the
judge's preprocessor would also not take. An unknown block is not removed at
all: every directive and every line of every alternative is kept, so the
judge's preprocessor evaluates it itself. The only remaining question is
whether the *state after the block* (used for later decisions) is still a
sound over-approximation of the judge's state. The resolver already answers
that for genuinely unknown conditions: the state after the block is the join
of the end states of every possible alternative (plus the implicit empty
alternative), equal states staying known, everything else unknown. The
judge's actual path is one of these alternatives, so every fact that stays
known after the join is the judge's fact. Demotion only feeds the resolver
less information than it had; it never adds a fact.

The local check is then meaningful again: under the chosen configuration
`c`, `c`'s preprocessor evaluates the demoted directives on its own, and every
remaining true/false decision of `a'` is reproduced (or proved dead) by `c`,
which is exactly the existing satisfaction relation. The preprocess check and
runs compare `raw` and `flat` as before.

## Dependencies: why re-run instead of patching decisions

Patching `a`'s decision for directive `d` to unknown without re-running would
be unsound:

- A `#define X` inside the formerly-taken alternative of `d` was applied to
  the resolver state unconditionally; a later `#if X` was decided from it.
  After demotion `X` is defined on one path and undefined on the other, so
  the later directive must become unknown (join). Only a re-run recomputes
  that. (Test: cascade case.)
- A formerly removed (false) alternative becomes reachable. Its nested
  directives were "dead" in `a` and have no decision at all; they must be
  walked.
- `#undef`, `#include <...>` invalidation and `#pragma GCC target` inside the
  newly-possible alternative change later states too.

Re-running the unmodified engine with `F` handles all of them by construction,
and the result is again a complete, internally consistent `ResolveResult`
(text, line map, `#line` resynchronisation, keep offsets).

Monotonicity is not needed for soundness (every iteration's result is sound on
its own) and not for termination (`F` grows strictly). It is what makes the
loop converge quickly in practice: forcing more directives unknown can only
turn known states into unknown ones, so a later directive's decision can only
move from true/false to unknown, never flip.

## Granularity: directive, not macro fact

Forcing a *macro fact* unknown (e.g. `__x86_64__`) instead:

- is too coarse: every directive that mentions the name is demoted, including
  ones on which `c` agrees, e.g. `#if defined(__x86_64__) || defined(__aarch64__)`;
- is not complete: a disagreement can come from something that is not a
  single external fact: the integer model (`#if LONG_MAX > 2147483647` on a
  Windows judge vs an LP64 host), the dialect, or a source macro whose value
  came from a block that itself disagreed. There may be no fact to demote;
- changes `transition()` behaviour for unrelated directives (an unknown
  identity macro stops being preserved across standard includes), which is
  harder to reason about.

Directive-level demotion is exactly the satisfaction relation's unit, so a
disagreement always has something to demote. Directives are keyed by their
line in the raw text: raw is fixed during the whole pipeline, so the
inventory and keys are identical across reruns (the same identity
`sameDecisions` already uses).

## Nested conditionals inside a demoted block

The design (§4.5) resolves conditionals nested inside an unknown block. For a
demoted block that would keep resolving, with judge facts, the conditionals
inside the alternative that only the judge takes. `c` never enters that
alternative, so those decisions are "dead" in `b` and the local preprocess
check and runs never exercise them. That is acceptable for genuinely unknown
blocks (existing design), but here we can do better at no soundness cost:
everything inside a demoted block is kept unknown, so the demoted block is
byte-identical to `raw`, which is precisely what would have been emitted
without demotion. Demotion therefore never introduces a resolution decision
that no check exercised and that the old fallback did not already rely on.
(Joins still track the macro state through the block.)

## Interactions

- **Include guards / `DISTILL_ONCE_*` (A3).** Guard macros are user or
  generated macros: undefined initially for both judge and `c` (A3, checked
  against each configuration's own facts; a collision disables that
  resolution, and a disabled rerun ends demotion with the old outcome). So
  guard directives never disagree. If one were demoted anyway, guard and
  body are kept verbatim and the joined state leaves the guard macro unknown;
  the next occurrence would stay unknown too. Sound, merely larger.
- **A5 facts.** Non-reserved names absent from the source are undefined for
  the judge (A5) and, for `c`, absent from its macro dump (A5's local
  counterpart): they cannot disagree. Demotion does not alter A5; the report
  only cites A5 for decisions still true/false.
- **`__has_include` / `__has_*`.** Invocations of non-known special builtins
  are already unknown and never demoted. A demoted block containing
  `__has_include` stays verbatim as before.
- **Assumptions and provenance.** A1–A5 are derived only from the final
  resolution's true/false decisions, so demoted decisions cite nothing.
  Demoted entries print as `kept (unknown: demoted: ...)` in the decision
  list, and each forced directive gets one diagnostic line:
  `flat: demoted <file>:<line> (#if): <kind> configuration disagrees`.
- **Verification level.** Unchanged rule: `target` only for a configuration
  equal to a reproduced judge; a cross-host local configuration is `local`.
- **Pruning.** Any unknown conditional disables pruning (existing rule,
  `unknownInUserCode`), with its reason in the report. So demotion yields
  `flat`, not `pruned`, on a cross host. That is the improvement over `raw`
  the task asks for; no pruning soundness argument changes.
- **First satisfying configuration is authoritative.** Pass 2 starts only if
  pass 1 found none, so it never replaces a satisfying configuration. Once a
  demoted resolution satisfies a candidate, that candidate is authoritative
  as before (a failing flat check gives `raw`, no further search).
- **Deadlines.** Each re-run receives the selection step's deadline; the loop
  checks it between iterations.

## Residual risks (not new)

- The judge-only alternative of a demoted block is checked by nobody locally,
  exactly as in `raw` and as before #90. Equivalence for the judge rests on
  keeping it verbatim.
- Cost: each iteration re-evaluates every condition (one evaluator call per
  condition). Typical runs converge in one or two iterations because all
  current disagreements are demoted at once; the step deadline bounds the
  worst case.
- Behaviour change: profiles that disagree with the host on a condition that
  only selects code (e.g. a Windows profile on Linux, the old
  `no_satisfaction` fixture) now give `flat` with a demotion line instead of
  `raw`. Intended.
