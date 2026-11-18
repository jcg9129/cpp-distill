<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Proposal: translation validation for pruning

Date: 2026-11-18. Phase 1 design and adversarial review only. The user has
chosen translation validation as the direction; this document proposes the
implementation boundary, refusal rules and outstanding decisions. It changes
neither the approved pruning policy nor code.

## Decision and existing contract

Build a conservative result **C** with today's rules, and optionally produce a
smaller result **P** whose additional deletions require semantic comparison
with Clang. Compare P against the checked, unpruned **F = flat.cpp**, not just
against C. A successful compilation is insufficient: deleting an overload can
change a successful program's constraint result without breaking its build.
All existing rendering, name, build and sample checks remain required on the
exact bytes selected for output.

Relevant sources read for this proposal:

- [Design](../design.md), especially §1.3, §4.6–4.9 and §6: A1–A7, parse and
  checking configurations, indivisible families, retention and checker limits.
- [Extractor](../../src/clang/prune_extract.cpp),
  [worker](../../src/clang/prune_worker.cpp),
  [worker orchestration](../../src/core/prune/extract.cpp),
  [retention](../../src/core/prune/prune.cpp),
  [renderer](../../src/core/render/render.h) and
  [pipeline](../../src/core/pipeline/pipeline.cpp).
- [Constexpr review, including the final A7 reassessment](2026-11-18-constexpr-inert-roots.md#under-a7-2026-11-18)
  and [ATL blockers](2026-11-17-atl-pruning-blockers.md).
- [Earlier operator/customization objection](2026-11-15-codex-design-r3.md)
  and [pruning review](2026-11-16-codex-pruning-r1.md): failed requirements,
  implicit operations, system-to-user lookup and missing AST edges.

`docs/reviews/2026-11-18-operator-reachability.md` is absent from this checkout.
The user's specified *unselected operator* counterexample is a required external
regression to import when available; the self-contained ambiguity example below
exercises that failure mechanism now. The earlier R3 compiler-branch example is
also required, but today's unresolved-conditional gate already refuses it.

### A8 and its limits

Proposed new assumption, reported verbatim with its identifier:

> **A8:** For retained code, GCC and Clang agree on overload resolution,
> template argument deduction and constraint satisfaction under standard C++
> semantics.

Here retained code includes implicit operations and system-header evaluations
that participate in retained user code, in both F and P. This is an assumption,
not something that two Clang parses establish. It does not assert agreement on
GCC extensions, library implementations, target layouts, arbitrary constant
evaluation, or every instantiation either compiler could perform.

The report adds A8 to the **written artifact's** dependencies only when it
actually omits a family C would keep (including families released transitively
by removing an operator root). An attempted or successful self-comparison alone
does not add A8. Conservative fallback retains today's A1–A7 dependencies;
flat/raw fallback does not inherit pruning assumptions. A8 is visible even with
verification `target`; that verification level still describes the checking
compiler/environment, not a Clang/GCC equivalence proof.

**Eligibility restriction:** do not use A8 to bridge an unexamined semantic
environment. For the first implementation require a judge-matching target,
dialect, audited semantic-option translations and the actual checking system
header inventory. With a judge profile require a reproduced checking
configuration; approximate/local fallback remains C. Without a profile this
mode may validate the explicit checking configuration, with a reported local
scope and no new claim about another judge.

Even these conditions do not make GCC and Clang take identical paths through
headers. Before enabling validation-dependent deletion, certify the relevant
system preprocessing surface against the checking compiler: compare normalized
preprocessed tokens and include occurrences for F under that compiler and the
Clang invocation, preserving header identity and token origins. Ignore only
the file/time spellings already excluded by §1.1 and representation metadata;
do not ignore type spellings, numeric constants, conditional branches or
builtins. Any unexplained difference refuses this mode. Audit any supported
normalization explicitly; do not falsify Clang's identity macros to force
equality. This may refuse many real libstdc++ configurations. Improving coverage
requires separate evidence, not a silent expansion of A8.

## Candidate construction and preservation obligations

Keep F immutable. Compute the existing family unions and full conservative
fixed point C. Preserve a rendered, fully checked C before optional validation
starts, so the expensive path cannot consume the opportunity to fall back.

Initially the only relaxation is removing the blanket `nonMemberOperator`
root for eligible ordinary non-member operator families. Recompute all other
rules to a fixed point. Never override `--keep`, macro pinning, source
directives, association, lexical names, AST dependencies, dependent-name
retention, record ownership, system specializations, attribute roots or
replacement allocation/deallocation roots. Hidden friends remain within their
whole record; there is no member pruning. Literal operators remain conservative
in the first implementation. Overload sets in a namespace remain indivisible;
do not remove individual unselected overloads from a retained family.

First target: a complete operator family whose concrete user parameter classes
are all absent from the tentative retained closure. A named user class-template
parameter form can qualify when its association is bounded; an unconstrained
catch-all operator or a family operating only on builtin/system types remains
conservative initially. Check eligibility after recomputing the tentative
closure, then restore ineligible roots and recompute until stable, with at most
eight restoration rounds under the optional-attempt deadline; use C if it does
not stabilize. This is a monotone root-restoration calculation, not repeated
semantic validation.

Call the additional deleted families **D = kept(C) minus kept(P)**. D contains
both released operator families and their released dependencies. Store the
causal group and exact byte ranges for each; do not label a released record
as an independently proven unused declaration. If D is empty, use C and skip
semantic validation. Roots for effectful initialization/destruction are not
relaxed by this proposal.

For every retained user declaration, pattern, expression, type occurrence and
implicit operation, compare its semantic result. Include retained unused
function bodies and non-dependent parts of retained template patterns, not
only code executed by `main`. Include all system semantic work with a retained
cause, including failed and speculative work. Preprocessing and rendering
invariants independently preserve the token and source-location contract.

Three comparison outcomes are mandatory: **equal**, **different**, and
**incomplete**. Unknown node kinds, missing observations, ambiguous mappings,
recovery ASTs, unsupported values and limits are incomplete, never equal.
Both different and incomplete reject P. A digest of an empty traversal is not
a certificate; each parse must furnish a coverage inventory.

## Identity and source mapping

Use physical byte/token identities in F, with the existing original-file/line
map for explanation. Presumed `#line` coordinates are not unique: repeated
inlining can give several tokens the same original location.

The renderer already returns `RenderedSource::copied` and `mapOffset` from
immutable input spans to output. Extend the mapping contract with a checked
inverse for surviving **tokens** and stable identities for include occurrences.
P is a subset of F's source tokens; its generated line markers and stripped
comment replacements are not literally a byte subset. Independently verify
that every retained token maps one-to-one with equal spelling, order and
intended location. An expression's endpoints and ordered token anchors must
survive. Do not map by searching for matching text, AST visitation order,
USR, mangled name, family vector index, or file/line alone.

Use distinct keys for identity and semantic payload:

| Entity | Stable key across F and P |
| --- | --- |
| Spelled user declaration | Surviving declaration-token anchor(s) in F, lexical owner key and declaration role; retain redeclaration occurrences separately |
| Expression/type occurrence | Enclosing declaration key, mapped token anchors and syntax role path; distinguish several nodes at one source range |
| Implicit node | Owning spelled node/declaration, semantic role (conversion step, cleanup, implicit member, etc.) and stable local ordinal within that role |
| Specialization | Primary declaration key, normalized complete template argument list, specialization kind and chosen pattern/partial/explicit specialization key |
| Instantiated node | Pattern node key, enclosing specialization stack and point-of-instantiation key(s) |
| System node | Header identity/content digest, include-occurrence chain rooted in a mapped include token, spelling offset, owner and instantiation context |
| Macro node | Mapped invocation key plus full spelling/expansion stack, argument/replacement token position and expansion role |

Find an entity's identity before comparing its type or selected specialization;
do not put the very result being tested into a key that causes it to disappear
from the comparison. A specialization argument change appears as removal and
addition under its retained demand site, which also must compare equal.
Implicit ordinals are never global AST numbering. Ambiguous role matching is
incomplete. Preserve multiplicity; equal source ranges can denote different
instantiations or implicit conversions.

Normalize canonical declaration references to source keys while preserving the
actual selected overload/specialization and redeclaration/default provider.
Within one parse canonical pointers are useful; across parses they are not
identities. Anonymous records, lambdas, unnamed parameters and local types use
their owning source anchor and role, not compiler-generated numbering. System
and resource headers must be frozen for the pair (including bytes, include
order, arguments and resource version); a changed input aborts comparison.

## Semantic fingerprint and coverage

Define a versioned structural record per retained semantic node, plus a
companion ledger of semantic events that do not survive in the AST. A canonical
encoding uses explicit tags, lengths, ordered children and stable references.
Compare full canonical records, using hashes only to accelerate matching.
Cycles use an interned graph of stable keys, not pointer values. Unknown enum
values or schema versions are incomplete. Sort unordered sets by key but
preserve order where it matters (arguments, conversion steps, lookup phases).

Every expression record contains its key, AST/syntax role, canonical type,
cv/ref qualifiers, value category, dependence/pack state, evaluation context,
children and applicable result fields. Types are structural: builtin kind and
width/signedness, qualifiers, references/pointers, bounds, functions and
exception specifications, declaration identities for records/enums, and
template arguments. Do not use `QualType::getAsString()` as equality. Also
preserve source-level alias/template selection where canonicalization could
hide a changed specialization.

| Semantic decision | Required payload |
| --- | --- |
| Calls and operators | Direct callee or indirect callee expression/type; member and virtual dispatch target/dispatch kind; builtin opcode; chosen overload/specialization; argument bindings; explicit object arguments; rewritten and reversed comparison candidate, operand permutation and underlying operation |
| Construction/conversion/address selection | Selected constructor/conversion/function; every implicit conversion sequence with source/target types, rank and conversion kinds, reference binding, qualification/base adjustments, list/narrowing decisions, user conversion plus before/after standard sequences; elision and temporary materialization; resolved overloaded address-of including contextual target type |
| Templates | Explicit and deduced arguments including packs, non-type values and template-template arguments; deduction success/failure and relevant attempts; selected class/function/variable specialization, primary/partial origin, CTAD guide and ordering/constraint result; default template/function arguments actually supplied, their provider and instantiated meaning |
| Constraints | Every evaluated concept-id, requires-expression and requirement; satisfaction/failure/deferred state, substituted arguments, evaluated atomic constraints and their source identities; substitution failure category; short-circuit evaluation status and unsatisfied requirement, without forcing unevaluated requirements |
| Computed decisions/values | Selected `if constexpr` branch; `static_assert` result; `noexcept` value and exception specification; `sizeof`/`alignof` value and operand type; constant initializers, enum values, bounds, non-type arguments and other values already required/computed by Clang |
| Names and types | Resolved declaration or overload set for declaration/member references, using shadows and qualified lookup; cheap ordinary/ADL lookup results and associated contexts; type/value/dependence state of unresolved/dependent names |
| Declaration semantics | Deduced/declared types, linkage, definition and specialization status, storage/TLS, relevant attributes, bases/fields/layout facts already computed, defaulted/deleted/trivial status, implicit special members, exception specifications, reference/ODR-use and required-definition relationships |

The conversion ledger is stronger than merely comparing the final cast AST.
Record Sema's selected implicit conversion sequence where available; a
reconstructed sequence needs an audited equivalence with that decision.
Likewise a `getDirectCallee()` comparison alone misses builtins, address-taking,
reversed comparisons, constructor selection and overload resolution in
unevaluated contexts.

Compare values only in the evaluation modes Clang actually used; do not call
`EvaluateAsRValue` on everything. That could instantiate new code, seed caches
or evaluate something in a different `is_constant_evaluated()` context. Values
need a lossless structural encoding: APInt width/signedness/bits, floating
semantics and bits, aggregate/union/array structure, and pointer/member-pointer
base declaration plus subobject path/offset. Unsupported APValue kinds refuse
validation. Distinguish unevaluated, deferred, failed and successfully computed
values; an absent value is never zero or false. Also compare evaluated
type-trait/builtin queries that can affect specialization or emitted code.

“Lookup where cheap” permits omitting a full candidate list only when a
completed decision is independently covered by the result fingerprint.
Failed requirements and unresolved/deferred lookup cannot be skipped on cost
grounds. For retained dependent patterns compare the definition-time binding,
lookup recipe and available overload sets where exposed; additionally retain
all potentially affected operator families when a future lookup cannot be
bounded. A changed lookup set may conservatively reject even when the chosen
callee is unchanged. Optimizing this over-rejection is later work.

### The AST is necessary and insufficient

`RecursiveASTVisitor` must explicitly cover implicit code, template patterns,
known instantiations, TypeLocs, defaults and synthesized operations. The
existing `Dependencies` graph supplies positive user/system edges; it does
not record every failed lookup or discarded substitution. A later AST walk
cannot promise to reconstruct those missing events.

Before enabling P, audit the pinned LLVM 23.1.5 interfaces for observing
overload resolution, deduction, substitution failures, constraint evaluation,
constant evaluation and template instantiation, including cached results.
Store semantic event keys (requesting node, arguments, context and demand
sites), outcomes and causal edges, not just a chronological trace.
Sema/AST callbacks may cover only part of this. If discarded results require
small LLVM instrumentation, that is an explicit build/distribution decision
and prerequisite; the current unmodified SDK must not be described as already
providing a complete event stream. A visitor-only first PR is observational
and cannot enable operator deletion.

Each parse emits counts and supported/unsupported categories for source
nodes, implicit nodes, instantiations, macro expansions, system evaluations
and Sema events. Caches must propagate the requesting site's dependency on
the cached result, even if no second evaluation occurs. Compare final semantic
outcomes/demands, not cache-hit counts, parse order or diagnostic wording.
Missing event coverage makes the pair incomplete.

## System headers, instantiations and deferred semantics

**Yes, system-header evaluations must be compared.** SFINAE, constraints,
unevaluated `decltype`/`noexcept` expressions and traits in a header can observe
user operators, even when no selected function refers back to the operator.
A6 includes operators but does not say headers cannot probe them. Filtering
all system nodes, or following only successfully selected callees, is unsound.

Start with all retained user nodes, required implicit operations and retained
directives/include occurrences. Follow an instrumented semantic demand graph
into system patterns, instantiations, unsuccessful candidate attempts and
constant evaluations, and follow references back into user code. Include
independent semantic events in retained header text as roots. Compute this
closure independently on both sides and compare their union by stable keys.
It is acceptable initially to compare too much system work; it is unsafe to
compare too little. Unknown causal provenance means include the event or refuse,
never discard it as “probably from removed code.”

Template patterns and their specializations are separate records. Retained
patterns' non-dependent semantics compare even without instantiation.
Instantiation records include the demanding site(s), arguments, selected
pattern, substitution outcomes and definition/point-of-instantiation contexts.
An instantiation present on only one side with a retained demand is a mismatch,
including instantiations needed solely for a losing overload or failed
requirement. Comparing only the intersection would hide the precise change
validation is meant to detect. A node created only on one side likewise
cannot be silently omitted because it has no partner.

An F-only instantiation may be ignored only with complete evidence that every
demand belongs exclusively to fully deleted families, no retained site uses
its cached result, and it introduces no declaration/state visible outside
those families. Otherwise reject P. Do not force such an instantiation into P
to make the sets match: that changes the question and may change lookup.

There are no arbitrary later source files in the supported single executable
TU. Nonetheless Clang and GCC, and the two parses, can defer work differently.
Equality of observed instantiations cannot prove equality of every possible
substitution. Keep today's dependent-name/association/record rules, plus a
conservative operator-exposure guard: a retained deferred lookup that could
observe D keeps the exposed operator families. This includes system template
patterns that can receive user arguments, unknown ADL associations, two-phase
lookup and default/member contexts. If the exposure cannot be bounded, use C.
Do not assume that an uninstantiated body is semantically irrelevant merely
because the Clang parse did not request it.

The guard must have useful negative proofs too. Check definition-time ordinary
visibility and possible ADL namespaces/classes (including bases, template
arguments and friends) for deferred operations reachable from retained demands.
An operator in a namespace associated solely with removed concrete parameter
classes, invisible to ordinary lookup and unreachable through retained argument
types, need not be kept just because a system header contains a generic `a+b`
pattern. Unused system patterns with no possible retained demand do not imply
arbitrary future source input. Conversely namespace sharing, conversions,
unknown argument flows and dependent associations defeat that isolation proof.
This bounded exposure analysis needs its own negative/positive tests; treating
every system pattern as exposed would make the int128 acceptance gate vacuous.

### Implicit operations, ODR and diagnostics

For retained classes/objects compare synthesis, deletion, triviality,
constraints and exception specifications of implicit special members, plus
constructors, destructors, conversions and cleanups at each use. Include
default member initializers, lifetime-extended temporaries, allocation and
deallocation selection, copy elision and destruction registration where
applicable. Unavailable lazy information is deferred with an exposure guard,
not obtained by forcing every implicit member into existence. Whole-record
retention and today's conservative storage-root classification remain intact.

Compare declaration references' evaluated/unevaluated and ODR-use states,
definition demands, emitted initialization/destruction obligations and
specialization instantiation status. For example an equal-looking constant
value must not hide a change from non-ODR-use to requiring an out-of-line
definition or a different destructor. This is semantic-front-end validation,
not IR or object-code equivalence; assembly, weak hooks and unsupported
attributes retain their existing roots/disables.

Errors, recovery nodes, invalid declarations and fatal/resource diagnostics in
either semantic parse reject P. Successful parses compare diagnostics tied to
retained sites by stable location, diagnostic ID, severity and semantic
arguments. New or removed warnings at retained sites conservatively reject;
warnings wholly owned by deleted code may disappear. Wording and ordering are
not behavior. The original checking compiler's exact command, including
warning/error policy, still builds each selected artifact: a Clang warning
comparison cannot stand in for a GCC `-Werror` build. Diagnostic output
truncation is incomplete. Diagnostic equality alone proves no semantic fact.

### Macros and location-sensitive code

Keep today's pinning of macro-produced declarations and macro-boundary edit
rules. Retained macro expressions still need fingerprints. One expansion
location can own many AST nodes; record both spelling and expansion chains,
nested invocations, argument substitution positions, pasted/stringized token
identity and the expansion's final token inventory. System macro expansions
that execute in user code are part of the retained graph too. A mapping that
collapses a replacement token to just its invocation's line is insufficient.

Compare F and P with the same virtual main-file identity and frozen header
buffers; use provenance for original filenames. Preserve logical lines under
recomputed markers and comment stripping. Existing gates for unknown
conditionals, external token pasting, predefined-value disagreements,
`__COUNTER__`, source-location builtins and directives remain. An ambiguous
macro/source mapping refuses validation; A8 does not repair preprocessing.

## Worker protocol, cost and fallback

Add a private bounded comparison operation alongside today's extraction
operation. Its input names F, exact rendered P, the checked token/span map,
retained family keys and D, and freezes the invocation/header inventory.
Bind the response to input byte digests, mapping digest, schema/policy and
LLVM/resource versions. Validate all counts, indices and ranges. Return a
small certificate/status, coverage counts and bounded mismatch examples;
never return an unbounded AST dump. The parent independently checks the input
binding and existing invariants before accepting `equal`.

Proposed first limits (subject to corpus measurements, not unbounded retries):

- One optional comparison attempt, two semantic parses, one of F and one of P,
  sequentially. The earlier dependency extraction is a separate parse, so
  initially at most three worker Clang TU parses total. Reusing it for fingerprinting
  can come later; never share AST pointers or mutable Sema caches between F/P.
- A **30-second total optional-attempt deadline**, spanning environment
  certification, candidate construction, mapping, parsing, comparison and P's
  checking build/samples.
  Limit the semantic worker to at most 20 seconds of the remaining allowance,
  reserving up to 10 seconds for P's checks; skip when the configured enclosing
  allowance is insufficient. No reset per parse, phase or failed group. Use
  today's process-group / Job Object containment and parent termination on
  timeout/crash. These are proposed budgets, not existing pipeline behavior.
- At most **1,000,000 semantic records/events** and **64 MiB canonical payload**
  per side; at most **1 MiB result stdout**, **4 MiB stderr**, and 20 mismatch
  examples. All existing input/model/protocol limits still apply. Bound recursion
  and graph work, including source-map and type normalization work, within this
  budget. Reaching a cap is incomplete, with the reason reported.
- Destroy each AST before building the next, retaining only canonical records.
  Record elapsed time, peak memory when available, and per-category counts.
  Keep the existing policy against a small blanket address-space cap; OS/OOM
  failure is contained and falls back. This is not a portable hard RSS bound.

Implement deadline checkpoints inside the worker as well as the parent's
hard kill. The optional step begins only after C's rendering invariants,
checking build and requested samples pass. Propagate the same optional deadline
through the pipeline instead of resetting it at each existing `step()` call;
any enclosing deadline also applies. Reserve P's build/sample allowance before
starting; otherwise skip P and use C. Selection of already checked C needs no
further parse/build. A timeout never promotes a partially compared P.

Fallback is deterministic and bounded:

1. Existing analysis failure takes today's flat/raw path. Failure of C's
   checks also follows that path; semantic comparison cannot rescue it.
2. Construct P and D once. Any mapping/invariant failure, ineligibility,
   different/incomplete fingerprint, worker failure/limit, or later P
   build/sample failure discards **all** D and selects checked C.
3. An equal pair plus successful P checks selects exactly those P bytes and
   attaches A8 and the certificate metadata. No later transformation is allowed.

Do **not** re-add one family after another in the first release. Equality is
not monotone in overload-set deletion: combinations can change ambiguity,
specialization ordering and constraints. A failing node need not identify a
single culpable family. An optional future repair mode could use at most four
deterministic, predeclared family groups, testing each cumulative candidate
fresh against F under the same total deadline; failed tests re-add their
entire proposed group, including closure dependencies. It must still end at C
when uncertain, and never use binary-search/monotonicity reasoning as proof.

### Report and assumption interactions

Report conservative bytes/families, attempted additional removals and their
causes, validation status (`not needed`, `ineligible`, `equal`, `different`,
`incomplete`), timing/budget/coverage, configuration and header/schema digests,
and actual selected result. Show original source locations and semantic role,
with before/after summaries for mismatches. Distinguish a comparison failure
from a later checking-build/sample failure. Families re-retained after a failed
attempt say `validation fallback: ...`; ordinary removal reasons remain intact.

Example, illustrative rather than a measured outcome:

```text
pruning validation: equal; schema=1; optional parses=2
family "operator+": removed: semantic validation (A8)
family "soft_int128": removed: dependency released by operator family (A8)
output assumptions: ... A6 A7 A8
```

On rejection the attempt remains in analysis, while output assumptions omit
A8. A validated candidate later rejected by GCC or samples also contributes
no A8 to C/flat/raw. No claim of whole-program proof or minimal assumptions is
made by the certificate.

**A6 stays:** preserve the versioned ADL-name list and all non-operator
customization/specialization rules. Validation replaces only the eligible
operator blanket root, not A6 or its system-header scope. Its operator clause
makes system probes mandatory. Header coverage outside the audited scope still
disables pruning. A8 does not manufacture missing judge-header observations.

**A7 stays:** every pruned artifact carries A7 under today's policy, including
C and P with no additional deletion. It excludes limit/cache-dependent
observable behavior, not all missing events or all stateful metaprogramming.
The validator must still detect changed computed results when possible and
reject on resource exhaustion. No new constexpr-depth/operation option
translation or broader constexpr/constinit storage exemption is authorized.

## Test and acceptance plan

The decisive negative test removes an operator that is **not selected** in
the original parse, yet changes semantics. Both versions must compile:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
namespace left { struct X {}; }
namespace right { struct Y {}; }
#ifndef REMOVE_LEFT
namespace left { int operator+(X, right::Y) { return 1; } }
#endif
namespace right { long operator+(left::X, Y) { return 2; } }
template<class A, class B>
constexpr bool adds = requires(A a, B b) { a + b; };
int main() { return adds<left::X, right::Y> ? 7 : 0; }
```

With both operators, ADL finds an ambiguous expression and `adds` is false;
neither operator is selected. Removing the left operator makes it true and
changes exit status from 0 to 7. Association normally retains these families;
use the existing style of forced-bad-removal test hook to test the validator
independently of that protection. Assert a failed-requirement/constraint
mismatch and fallback, not just a compiler error. Repeat with the probe inside
a synthetic audited system header and import the user's exact operator-review
fixture when available. Do not mark that exact external fixture covered by
this substitute.

Required coverage, with each negative asserting its checker/refusal and output:

| Group | Cases and expected result |
| --- | --- |
| Self-comparison | F/F and F rendered with no deletions: equal, complete coverage, deterministic records across fresh processes; retained unused bodies/templates, headers, implicit code and macros present; no A8 output dependency |
| Identity | Same-line nodes, repeated inlined copies with equal `#line`, CRLF, stripped comments, generated markers, lambdas, macro nesting/arguments/paste and system macro uses; collisions/missing map entries refuse |
| Selection/conversions | Call/operator/constructor/conversion/address selection; same callee but changed conversion/default provider; rewritten `<=>`/`==` and reversed operands; builtin versus overloaded operation; indirect calls |
| Templates | Function/class/variable and partial/explicit specializations, CTAD, deduction failures, packs, template/non-type overload arguments, defaults, constraints on losing candidates, short circuit and original-only instantiations; compare demands in both directions |
| Values/types | Requires/concepts, constexpr branch, assertions, exception specifications, layout/traits/size/alignment, enum/bounds/constant initializers; unchanged value with changed type/ODR-use; actual computed values only |
| System/lazy work | System SFINAE probes of user names/operators, cached satisfaction reused at retained sites, deferred two-phase lookup, implicit deleted members/destructors, default member initializers, lifetime extension and friend injection; mismatch or explicit incomplete fallback |
| Failure containment | Error/recovery AST, unsupported node/value/event, divergent system tokens, changed header bytes, malformed/truncated protocol, timeout during either parse/canonicalization, cap/OOM/crash, P build/sample failure: exactly checked C, no A8 |
| Reports | D empty, no deletions, `--no-prune`, ineligible config, passed/rejected/limited validation, local/target, strict mode and flat/raw fallback; assumption and family-byte attribution tied to selected artifact |

Positive acceptance requires an **ATL-like unused software int128** fixture:
an ordinary user record with arithmetic/conversion helpers and namespace
operator families, alongside an unrelated retained algorithm. Assert that
removing eligible operator roots removes the complete unused type/helper
closure, comparison is equal and complete, GCC builds/runs match, and output
has A8. Add controls where an int128 operation, overload-address argument or
system constraint actually needs it: retain it or reject deletion. Keep a
no-system-header version to isolate the optimization from environment refusal,
and a real libstdc++ version for the supported matrix.

An actual ATL corpus measurement is a release gate, with selected profile,
header/compiler versions, kept-byte attribution and before/after outputs. No
ATL sources are present here; no measured reduction is claimed. If namespace
overload aggregation or another root keeps that implementation alive, report
the blocker rather than weakening the family model in this PR. The synthetic
positive must actually remove int128; an always-fallback implementation does
not satisfy acceptance.

Run today's retention, extraction, renderer, pipeline and checker suites,
then the new pinned-worker tests. Compile/run counterexamples under supported
GCC and Clang configurations; label A7 violations separately. Add mutation
tests that force wrong callee/constraint/type/default/conversion records or
delete a required ledger entry: the last must yield incomplete, not equal.
Measure `<bits/stdc++.h>` and the ATL fixture under default budgets; store
results per supported dialect/target/library. Each test has a hard timeout;
optional sample agreement never replaces a missing semantic observation.

Phase 1 sanity check of the displayed ambiguity fixture: GCC 15.2.0 and
Clang 22.1.8 both compile the original and `-DREMOVE_LEFT` variants with
`-std=c++20 -O0 -pedantic-errors`. Both originals exit 0; both deletion
variants exit 7. This verifies the counterexample's mechanism, not a working
validator or the pinned LLVM 23.1.5 extraction path. No implementation or
measured int128 pruning is claimed in this design-only change.

## Small PR sequence

1. **Identity and fingerprint extraction:** versioned schema, checked mapping,
   canonical types/values/declarations, explicit coverage inventory and F/F
   tests; keep all existing operator roots. Audit Sema observation gaps against
   the pinned SDK and add negative coverage tests. Any necessary LLVM hooks
   need their own small reviewed SDK/build change before the next step.
2. **Comparison and bounded fallback:** bidirectional demand comparison,
   system-header and deferred-exposure gates, frozen inputs, resource/protocol
   limits and forced-bad-removal tests. Then relax eligible operator roots only
   behind a complete equal certificate; require the int128 positive. Include
   minimal A8/report propagation in this PR so no enabled artifact can depend
   on an unreported assumption. Default to conservative until acceptance passes.
3. **Report, A8 and documentation completion:** expanded mismatch explanations,
   byte attribution, assumption/help/design text, configuration matrix and
   measured budgets. This polishes reporting already required by PR 2; it
   cannot postpone A8 disclosure until after the optimization ships.

## Adversarial review

The following attacks change the proposed boundary. They are not waived by
having a broad list of fingerprint fields.

1. **Only successful AST edges were compared.** The ambiguity example changes
   a requirement without selecting either operator originally. A system SFINAE
   probe may leave no user reference. **Adjustment:** failed/speculative Sema
   events, system evaluations and explicit coverage are prerequisites. A
   visitor-only implementation has no permission to delete operators.

2. **GCC-specific behavior, despite equal Clang fingerprints.** GCC builtins,
   extension overload rules, attributes, inline assembly or different compiler
   branches in libstdc++ can observe declarations that Clang never sees. A8's
   standard overload/deduction/constraint agreement says nothing about those
   mechanisms. Even standard code may query a target-dependent layout/value.
   **Adjustment:** preserve existing disables, restrict the semantic environment,
   require the preprocessing-surface check, and reject unsupported retained or
   demanded extension paths. Do not call a Clang equality a GCC proof. If the
   real header matrix cannot pass, broader support needs a separate decision.

3. **Uninstantiated templates become relevant in system code later.** A
   retained system template can receive a user type through a default or trait;
   an operator unused in Clang's observed bodies can then enter ADL. GCC might
   request the instantiation at a different point. **Adjustment:** fingerprint
   retained patterns and definition-time bindings, keep dependent-name and
   association safeguards, and retain D exposed to unresolved future lookup.
   A8 is not an assumption that the two compilers instantiate identical sets.

4. **Two-phase lookup at an original-only instantiation point.** Pairing by
   specialization name alone can hide a missing demand, or matching only P's
   AST can drop an original losing candidate's instantiation. **Adjustment:**
   compare the union of retained demands with points/contexts of instantiation;
   original-only or candidate-only demanded work is a mismatch. Removed-only
   work needs a complete isolation certificate before exclusion.

5. **An overload set is passed as a template argument or used as an address.**
   The final call could have the same callee while `auto F`, a pointer/member
   pointer non-type argument, a template-template argument, or contextual
   address resolution changes the specialization. **Adjustment:** normalize
   argument declaration identities, types, values and overload-set/contextual
   selection records; compare deduction attempts and specialization origin.
   Preserve whole overload families. Textual names or mangled calls alone fail.

6. **Friend injection or stateful metaprogramming changes lookup history.**
   Instantiating a seemingly deleted family may inject a namespace friend;
   another retained probe can observe it without a direct edge to its source.
   Compile-time friend counters are not necessarily limit/cache failures, so
   A7 does not exclude them. **Adjustment:** instrument declaration introduction
   and lookup visibility, with causal owners. Keep affected families for
   dependent friend injection, history-sensitive probes or unbounded visibility;
   refuse when isolation of removed-only work cannot be shown. Do not equate
   “no selected references” with no semantic side effects.

7. **Normalization erases the difference.** Two aliases canonicalize to the
   same type while their selected specialization/default differs; implicit nodes
   share a source range; pointer-valued APValues lose their subobject identity.
   **Adjustment:** separate stable keys from results, preserve selection origins,
   role/multiplicity, declaration-backed values and exact conversion details.
   Never match ambiguous nodes opportunistically or compare hash alone.

8. **Lazy implicit semantics and ODR-use escape.** Same expression type/value
   can hide a deleted move constructor, exception specification, destructor or
   new definition demand. **Adjustment:** include implicit operations and
   ODR-use/demand records; retain records whole and storage roots unchanged.
   Unobserved lazy semantics exposed to D force retention/refusal, not invented
   successful values from extra evaluation.

9. **Fingerprint collection changes the program being checked.** Forcing all
   templates or constant expressions can change friend state, cache use or
   `is_constant_evaluated()` outcomes. **Adjustment:** observe the normal parse
   in its original contexts, record deferred states, and use exposure guards.
   The collector must pass noninterference tests; absence of hooks is a blocker.

10. **Bounded repair accidentally promotes an unchecked subset.** Re-adding
    one operator can expose a third candidate, so an earlier comparison no
    longer applies. Worker timeouts can also arrive after only one side is
    complete. **Adjustment:** one attempt and whole-D fallback initially;
    certificates bind exact bytes, mapping and configuration; every result
    except complete equality rejects. Later group repair needs fresh full
    comparison for every candidate and a fixed total budget.

### Explicit verdict

**Proceed with observational fingerprint extraction and self-comparison tests.
Do not enable validation-dependent operator removal until the coverage,
environment, deferred-lookup and positive int128 gates above are demonstrated.**
The adjusted design is a conservative conditional validator under A1–A8, not
a general semantic-equivalence algorithm. The user-selected direction can
remove unused operator/type closures, but two matching AST dumps alone cannot
satisfy the requested contract. Missing Sema observations or unbounded future
lookup must produce C, never an optimistic certificate.

### Open questions for the user

1. Approve the initial environment restriction (reproduced judge configuration
   and conservative fallback on unexplained system-token differences), accepting
   that it may initially refuse many real GCC/libstdc++ cases? Broader library/
   cross-environment equivalence would need evidence or an explicit additional
   assumption; it is not covered by A8 as written.
2. If LLVM 23.1.5's exposed APIs cannot observe discarded semantic work, is a
   small maintained Clang instrumentation patch acceptable, or should this
   remain observational until an unmodified SDK can meet the contract?
3. Approve one validation attempt, whole-D fallback, and the proposed
   30-second/record/payload caps initially? Group repair is deferred.
4. Which exact operator-review fixture and ATL software-int128 revision should
   become release acceptance inputs? The local ambiguity fixture is a substitute
   mechanism test, not a claim to have read or reproduced those missing inputs.
