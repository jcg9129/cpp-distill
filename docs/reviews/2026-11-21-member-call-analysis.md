<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Member call retention analysis

Date: 2026-11-21 (measurement session started 2026-11-20 22:28 UTC).
Analysis only; no production behavior change. Base `68e29302a62f84f80a04ecd11c93629eead83272`
(PR #106), branch `calls-analysis`. ATL is the read-only clone at
`5e2a6e2b6f4c85219ec5db0b6f1f1bbd94900fb8`, containing 85 examples.

## Recommendation

Across all 85 examples, **3,086 ordinary retained units occupy 787,849 bytes**.
Only **18 units / 6,387 bytes** are retained by unrelated non-dependent bindings,
and **23 units / 2,095 bytes** by dependent sites whose observed targets are all
elsewhere. Together those locally unused candidates occupy **8,482 bytes
(1.08%)**. This is direct member-unit accounting; it neither proves removal safe
on another target nor measures additional savings from nonmember helper closure.
Another **801 units / 164,127 bytes** have independent A12-only protection, and
**221 units / 45,852 bytes** have unresolved dependent sites. No macro-only or
unused-cycle/cascade units were found. Rule 1 is excluded from these figures.

Refine forward rule 5 first with **proof of a target-stable binding**, starting
with local variable/parameter declaration names and unqualified references
to those lexically fixed declarations. Qualified friends and using declarations
remain lookup-bearing references. Keep name retention for dependent, opaque and potentially
target-selected lookup. In particular, **“all worker instantiations selected
another declaration” is not an all-target removal proof**. Apply the same caution
to non-dependent member expressions whose receiver type comes through an alias,
`auto`, `decltype`, a selected base or overload resolution.

A12 inventory retention is independent: changing rules 5/6 cannot remove an
inventory-protected member. Preserve the conservative reverse potential
instantiation-effect graph over all text; refining forward lookup does not prove
that a discarded declaration cannot instantiate an effect on another target.
The current static-name invariant must also be redesigned for a refined rule:
it deliberately rejects a removed member name even when surviving occurrences
are unrelated local variables. No such policy changes were made here.

## Measurement and attribution

Every example uses the CSES profile, C++23, `-O2`,
`--strip-comments --no-line-markers`, and default A11/A12, following the `nlm`
convention in `ci/atl-sweep/sweep.sh`. This is local verification with GCC 15.2
x86_64 headers, not a CSES judge execution or matching GCC 13.3 header capture.
A12 uses the actual worker headers plus **14 scanned builds, generator 4**;
the committed scanned union has **32,555 names**.

The unmodified report records the *first* reason encountered. The temporary
extractor diagnostic instead records resolved references, declaration bindings,
dependent-expression sites, active preprocessor tokens and the complete model,
then independently computes retention. It never changes that model or its
production retention decisions. The diagnostic fixture emits exactly the same
artifact bytes and family/member report lines as the baseline binary. The full
process-log sections differ in temporary paths and executable locations.

Units are grouped by `(union-find family group, member spelling)`, exactly like
the production report, including overloads, redeclarations and specializations.
Only units of kept user records are counted. Rule 1 units are excluded from the
classes and reported only as an aggregate total. Each unit's bytes are its
unique, disjoint, owned ranges in the comment-stripped worker input. They are
not net output savings; all overloads count when any overload is referenced.
Totals sum example occurrences, without deduplicating shared ATL headers across
examples. Structural bytes and nonmember families are outside this table.

Final retention filters all user witnesses. A unit's own union group never
counts as an external witness. System positive references count only through
system entities reachable from kept user sources. Resolved expressions and
using/type references count as AST evidence; an unresolved overload's candidate
set alone does not. “Referenced” does not mean “executed at runtime.”

The exclusive precedence is: rule 1 exclusion; global pruning refusal (R-other);
resolved AST reference; independent roots/pins, virtual clash, reserved names,
explicit instantiation or language lookup protection (R-other); independent A12
inventory protection (R-lib); dependent sites; unrelated resolved tokens;
unparsed-only tokens; remaining opaque/mixed evidence (R-other).
Thus R-lib means the inventory is the remaining independent protection after
semantic references/other mandatory rules, even if unrelated source tokens also
match. Such bytes cannot be saved by refining rule 5 alone.

For dependent sites, pattern and instantiated references are joined by physical
identifier offset and name. (a) requires at least one resolved instance at
**every matching kept site**, with all targets outside the candidate unit.
Missing sites, unobserved instances, dependent declarations without a resolved
using-shadow, and macro sites lacking a distinct raw identifier location
conservatively fall in (c).
Recorded declaration/using-shadow pairs recover dependent `using` sites at
their physical identifier locations. Production `dependentNames` entries still
without a site are explicitly covered by the unresolved fallback. Other retained declarations/local variables with the
same spelling do not make a dependent site resolved.

**R-dep(b) overlaps R-AST by definition:** an instantiation resolving to this unit
is positive AST evidence. R-AST wins in the exclusive table, so its exclusive
R-dep(b) row is zero. The separate dependent-outcome tables report (b)'s observed
units/bytes and top names per example; those numbers must not be added again.

R-cycle checks provenance as well as the worklist algorithm. Independent
mandatory/library units, roots, unresolved dependent/opaque cases and kept
nonmember families anchor a second graph of resolved member references. A unit
with references only from locally unused collision units is classified R-cycle,
including a cascade. No unknown dependent case is presumed unused. Positive system-dispatch targets are also conservative anchors. This is a
local diagnostic counterfactual, subject to the same cross-target caveat as
R-nondep-other; it is not a change to effect analysis.

The production closure is a **least fixpoint from roots**, not an initially full
set with iterative deletion. Name-only retention waits for a kept owner. An
unrooted mutually referencing component cannot bootstrap itself. The fixture
with two mutually calling unused methods removes both; another fixture puts a
27-byte helper called only by a collision-retained method in R-cycle. Category
fixtures also distinguish actual references, foreign local bindings, dependent
(a)/(c), macro-only tokens and A12-only retention.

## Concrete observations and cross-target risk

`coordinate-compression/basic` and `coordinate-compression/move-only` each retain
a **1,987-byte `atl::coordinate_compression::ranks`** unit. Every external `ranks`
token binds to the data field `atl::compressed::ranks` or a local variable,
including the field access in `main`; none resolves to that function unit.
This is a concrete R-nondep-other collision. Removing only that additional unit
from each production retention result compiles standalone with x86_64 GCC and
matches both expected sample outputs under QEMU.

A further **75-byte `atl::detail::heap_directory::heap_count`** unit in
`meldable-heap-forest/basic` is also R-nondep-other. Its `atl_assert(heap_count ... )`
argument required a manual join from the raw argument location to the recorded
macro-expansion reference; that reference binds the constructor parameter,
not the member function. All other matching tokens bind parameters/local
variables too. This audited join is recorded explicitly in the classifier.

`convolution/quick` retains **71 bytes of
`atl::detail::montgomery64::from_form`** through dependent
`mod_ops::from_form(value_)` in `atl::mod_int::value`. Every worker instance of
that kept site targets `atl::static_modulus::from_form`, rather than this unit;
there are no independent A12/root protections. This is R-dep(a). Its temporary
removal also compiles and matches the local sample. These three experiments
validate the observed bindings, not universal removability.

ATL contains the relevant semantic selection mechanisms:
`nt/dynamic_mod_int.hpp:43` selects `barrett64`/`barrett` with
`std::conditional_t<sizeof(T) == 8, ...>`, and
`nt/ntt_engine.hpp:315` selects an engine or `scalar_ntt` through
`Engine::supports<Z>()`. This audit does not claim that the measured ATL collision
itself is needed on a different target. The following constructed cases establish
the general risk without headers or assumptions about a particular ATL alias:

```cpp
template<bool, class A, class B> struct pick { using type = A; };
template<class A, class B> struct pick<false, A, B> { using type = B; };
struct Eight { int ra_chosen() { return 8; } };
struct Four  { int ra_chosen() { return 4; } };
using Chosen = pick<sizeof(long) == 8, Eight, Four>::type;

int nondependent() {
    Eight a; Four b; (void)a; (void)b; // both records survive
    return Chosen{}.ra_chosen();
}
template<class T> int dependent(T t) { return t.ra_chosen(); }
// Independently test main calling nondependent(), or dependent(Chosen{}).
```

On LP64, the non-dependent call resolves to `Eight::ra_chosen`, so `Four`'s unit
has only a foreign binding (R-nondep-other). The dependent variant likewise has
only `Eight` worker instances (R-dep(a) for `Four`). On ILP32, both require
`Four::ra_chosen`. The expanded user identifier tokens are unchanged; `sizeof`
selection is C++ semantics, so preprocessing equivalence does not exclude it.
User target-macro branches already fixed by the profile remain fixed; this
counterexample does not rely on changing such a branch. System-header-selected
typedefs and types may still differ under the documented contract.

An overload/typedef variant replaces the `Chosen` alias with:

```cpp
using Word = pick<sizeof(long) == 8, long, int>::type;
Eight choose(long); Four choose(int);
using Chosen = decltype(choose(Word{}));
```

The worker picks `choose(long)` and the ILP32 target picks `choose(int)`, changing
the record containing the required same-named member. A system typedef could
produce the same change. All **12 syntax/deletion checks** passed with Clang 22:
for each of the non-dependent, dependent and overload variants, the full source
compiles for x86_64 and i386; deleting only `Four::ra_chosen` still compiles for
x86_64 and fails for i386. These are actual Clang target checks, not i386 runtime
checks. There is no sound rule “foreign on the worker implies removable.”

## Build, checks and evidence

Downloaded the pinned LLVM **23.1.5** Linux ARM64 SDK from the project's
`llvm-23.1.5-1` release and verified its published SHA-256:
`5ca712f18c710592a8fa89758246a6b6cbdbd16c1fbd5f12c7f5bcdf35fe06de`.
It was extracted under ignored `spike-work/`, built with Clang 22.1.8,
`-O1 -DNDEBUG`, LLD 22 and **`-j2`**, and deleted after the build. Configure/build
caps were 120/900 seconds; the diagnostic rebuild had a 600-second cap.

The native AArch64 trial encountered ATL's x87 conditional at `utils/hash.hpp:244`
and fell back to flat because it disagreed with the CSES snapshot. The reported
sweep therefore uses a worktree-local native-executing x86_64 GCC 15.2 cross
compiler. All 21 downloaded cross-toolchain/QEMU packages were SHA-256 checked
against public Ubuntu package indexes and extracted inside the worktree. Nothing
in the ATL clone was modified.

Generation is bounded by 180 seconds per example, 2,500,000 KiB virtual memory,
and an overall sweep timeout; two examples run concurrently. Target/deletion
checks have 30-second caps; standalone candidate compilation/execution caps are
90/30 seconds. The requested 50-minute session budget includes acquisition,
build, analysis, reporting and cleanup.

Six focused CTest checks passed across the initial run and rerun: `unit`,
`embedded_members`, `prune_extract_cases` (84 fixture runs), `prune_extract_models`,
`member_pruning` and `member_review`. The first extraction-case run overlapped
worker relinking and had two temporary executable-not-found failures; the
isolated rerun passes all 84 cases. Measurement/SDK Python tooling passes 16
tests. The category/cascade fixtures, 12 cross-target deletion checks and three
candidate-removal sample checks pass. Baseline/diagnostic artifact bytes match.
Additional unmodified sweep sample checks are counted below. This is targeted
validation for analysis, not a new full-repository test certification.

Production sources have been restored; this Markdown file is the only intended
Git change. The exact temporary patch is retained at
`measurements/member-call/diagnostic.patch` (ignored local report directory),
with acquisition/checksum metadata, scripts and evidence. Raw models, reports,
artifacts and logs are in `.checks/member-call/`; no credentials were sought.
All 85 invocations exited successfully and emitted a complete diagnostic model.
**84 artifacts are pruned; one is the existing fast-I/O flat fallback. All 85
standalone GCC builds and QEMU sample-output comparisons pass.** Every model's
retained unit count and bytes reconcile exactly with the production report;
no union group mixes different member names or a member with a nonmember.
The only instantiation-effect source is in `modular-arithmetic/residues`, whose
ordinary units already have stronger references or other independent protection;
no collision/dependent candidate there relies on an unreported effect reason.
The closure/cascade audit finds zero R-cycle units. The SDK is absent after
cleanup, and `git diff --check` passes.

The final tables cover the full 85-example inventory. The local diagnostic patch
SHA-256 is `73e833748fdbe3f97890e2264346282ad07b59d1680337c504539827bf1c6041`.




<!-- MEASUREMENTS -->

## Totals (all 85 examples)

Completed generation and classification: **85/85 examples**.

| Strongest reason | Units | Member bytes | Top names: bytes (units) |
| --- | ---: | ---: | --- |
| R-AST | 1,863 | 542,969 | `build` 26,734 (26); `descend` 17,344 (9); `reduce` 15,339 (53); `mul` 14,084 (60); `add` 12,047 (48); `insert` 11,900 (14); `build_from_parents` 11,280 (4); `forward_with` 9,999 (11) |
| R-nondep-other | 18 | 6,387 | `ranks` 3,974 (2); `grouped` 863 (1); `groups` 391 (1); `mod` 215 (5); `split_at` 206 (1); `same` 194 (2); `points` 155 (1); `stamp` 111 (1) |
| R-dep(a) | 23 | 2,095 | `neg` 1,020 (10); `from_form` 710 (10); `splay_pushed` 288 (2); `add_of` 77 (1) |
| R-dep(b) | 0 | 0 | — |
| R-dep(c) | 221 | 45,852 | `incr` 3,519 (34); `decr` 2,839 (34); `build` 2,540 (2); `build_sorted` 2,288 (2); `grow` 2,180 (4); `adopt` 2,180 (4); `inverse` 2,122 (22); `reserve_extra` 1,980 (5) |
| R-macro | 0 | 0 | — |
| R-lib | 801 | 164,127 | `at` 11,017 (22); `power` 8,977 (47); `erase` 8,207 (19); `find` 6,910 (14); `resize` 6,816 (16); `reserve` 6,138 (36); `for_each` 6,107 (8); `contains` 5,332 (21) |
| R-cycle | 0 | 0 | — |
| R-other | 160 | 26,419 | `begin` 3,711 (35); `end` 3,432 (31); `power` 1,352 (8); `parse_uint` 1,170 (1); `put` 1,162 (1); `add` 1,152 (5); `mul` 1,142 (5); `reduce` 867 (3) |

**Rule 1 excluded kinds, separate total:** 838 units, 237,583 bytes.
Classified ordinary units: 3,086; ordinary member bytes: 787,849.

### Per-example counts / bytes

Each cell is **units / bytes**. `AST`, `ND`, `Da`, `Db`, `Dc`, `M`, `L`, `C`, `O` follow the class order above.

| Example | AST | ND | Da | Db | Dc | M | L | C | O |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| assignment-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| bipartite-matching-dag | 14 / 3,743 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 139 | 0 / 0 | 0 / 0 |
| bipartite-matching-quick | 12 / 3,030 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 139 | 0 / 0 | 0 / 0 |
| bit-utilities-combinations | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| bitwise-convolution-quick | 20 / 3,190 | 0 / 0 | 0 / 0 | 0 / 0 | 8 / 722 | 0 / 0 | 4 / 709 | 0 / 0 | 0 / 0 |
| centroid-decomposition-quick | 4 / 942 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 1,862 | 0 / 0 | 5 / 636 | 0 / 0 | 0 / 0 |
| convolution-quick | 36 / 8,528 | 0 / 0 | 1 / 71 | 0 / 0 | 11 / 947 | 0 / 0 | 18 / 2,795 | 0 / 0 | 2 / 216 |
| coordinate-compression-basic | 2 / 384 | 1 / 1,987 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 348 | 0 / 0 | 2 / 126 |
| coordinate-compression-move-only | 2 / 384 | 1 / 1,987 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 348 | 0 / 0 | 2 / 126 |
| counting-sort-stable-records | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| crt-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| difference-fenwick-basic | 14 / 5,224 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 176 | 0 / 0 | 0 / 0 |
| discrete-log-quick | 44 / 9,808 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 207 | 0 / 0 | 37 / 7,896 | 0 / 0 | 4 / 464 |
| dsu-history | 29 / 9,022 | 2 / 194 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 10 / 990 | 0 / 0 | 0 / 0 |
| dual-segment-tree-quick-start | 9 / 2,651 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 343 | 0 / 0 | 0 / 0 |
| dynamic-lazy-segment-tree-quick-start | 12 / 4,978 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 569 | 0 / 0 | 0 / 0 |
| dynamic-segment-tree-quick-start | 5 / 2,618 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 878 | 0 / 0 | 0 / 0 |
| euler-tour-tree-basic | 68 / 21,203 | 1 / 206 | 0 / 0 | 0 / 0 | 3 / 1,133 | 0 / 0 | 19 / 5,720 | 0 / 0 | 4 / 508 |
| factorization-quick | 12 / 1,885 | 3 / 149 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 20 / 3,620 | 0 / 0 | 2 / 216 |
| fast-io-roundtrip | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 71 / 16,243 |
| fenwick-tree-basic | 9 / 3,928 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 209 | 0 / 0 | 0 / 0 |
| flat-hash-basic | 15 / 5,216 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 160 | 0 / 0 | 29 / 7,803 | 0 / 0 | 6 / 634 |
| floor-sum-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| gcd-bezout | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| graph-callbacks | 9 / 1,420 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 64 | 0 / 0 | 0 / 0 |
| graph-components-blocks | 16 / 4,562 | 1 / 863 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 139 | 0 / 0 | 0 / 0 |
| graph-components-bridges | 14 / 4,022 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 139 | 0 / 0 | 0 / 0 |
| graph-components-scc | 12 / 3,249 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 139 | 0 / 0 | 0 / 0 |
| graph-quick | 15 / 3,930 | 1 / 391 | 0 / 0 | 0 / 0 | 1 / 205 | 0 / 0 | 3 / 269 | 0 / 0 | 0 / 0 |
| hierarchical-bitset-basic | 6 / 1,727 | 1 / 69 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 5 / 1,031 | 0 / 0 | 0 / 0 |
| int128-arithmetic | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| lazy-segment-tree-quick-start | 13 / 3,863 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 294 | 0 / 0 | 0 / 0 |
| lca-path-sum | 13 / 7,931 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 1,090 | 0 / 0 | 10 / 1,189 | 0 / 0 | 0 / 0 |
| lca-quick | 7 / 5,738 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 1,090 | 0 / 0 | 5 / 1,535 | 0 / 0 | 0 / 0 |
| li-chao-tree-basic | 18 / 11,100 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 296 | 0 / 0 | 0 / 0 |
| linear-recurrence-quick | 41 / 9,129 | 0 / 0 | 1 / 71 | 0 / 0 | 16 / 1,514 | 0 / 0 | 23 / 3,483 | 0 / 0 | 2 / 216 |
| link-cut-tree-basic | 78 / 24,906 | 0 / 0 | 1 / 144 | 0 / 0 | 3 / 880 | 0 / 0 | 31 / 9,378 | 0 / 0 | 4 / 508 |
| matrix-quick | 35 / 5,218 | 0 / 0 | 0 / 0 | 0 / 0 | 11 / 958 | 0 / 0 | 11 / 1,893 | 0 / 0 | 0 / 0 |
| max-flow-bounds | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| max-flow-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| meldable-heap-forest-basic | 18 / 4,885 | 1 / 75 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 901 | 0 / 0 | 0 / 0 |
| min-cost-flow-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| min-cost-flow-supplies | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| modular-arithmetic-residues | 30 / 4,502 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 23 / 3,033 |
| multiplicative-functions-quick | 1 / 122 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 11 / 1,634 | 0 / 0 | 2 / 216 |
| online-convolution-partitions | 41 / 13,550 | 0 / 0 | 1 / 71 | 0 / 0 | 10 / 866 | 0 / 0 | 22 / 3,402 | 0 / 0 | 2 / 216 |
| online-convolution-quick | 40 / 13,493 | 0 / 0 | 1 / 71 | 0 / 0 | 11 / 947 | 0 / 0 | 22 / 3,402 | 0 / 0 | 2 / 216 |
| persistent-array-versions | 24 / 9,623 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 547 | 0 / 0 | 0 / 0 |
| persistent-heap-basic | 21 / 6,136 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 773 | 0 / 0 | 15 / 2,251 | 0 / 0 | 0 / 0 |
| persistent-lazy-segment-tree-basic | 14 / 4,437 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 1,670 | 0 / 0 | 6 / 2,112 | 0 / 0 | 0 / 0 |
| persistent-list-basic | 16 / 3,819 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 396 | 0 / 0 | 6 / 769 | 0 / 0 | 2 / 158 |
| persistent-queue-deque-basic | 37 / 10,682 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 23 / 3,654 | 0 / 0 | 2 / 158 |
| persistent-segment-tree-basic | 13 / 3,732 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 736 | 0 / 0 | 4 / 941 | 0 / 0 | 0 / 0 |
| persistent-treap-sequence-basic | 38 / 9,930 | 0 / 0 | 0 / 0 | 0 / 0 | 10 / 7,045 | 0 / 0 | 13 / 4,625 | 0 / 0 | 0 / 0 |
| persistent-vector-basic | 67 / 21,270 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 2,861 | 0 / 0 | 21 / 5,744 | 0 / 0 | 2 / 173 |
| polynomial-gcd-pade | 39 / 8,830 | 0 / 0 | 2 / 173 | 0 / 0 | 8 / 662 | 0 / 0 | 17 / 2,676 | 0 / 0 | 2 / 216 |
| polynomial-gcd-quick | 39 / 8,830 | 0 / 0 | 2 / 173 | 0 / 0 | 8 / 662 | 0 / 0 | 17 / 2,676 | 0 / 0 | 2 / 216 |
| polynomial-points-quick | 51 / 18,429 | 1 / 155 | 2 / 173 | 0 / 0 | 9 / 735 | 0 / 0 | 17 / 2,676 | 0 / 0 | 2 / 216 |
| polynomial-points-sampling | 44 / 10,660 | 0 / 0 | 2 / 173 | 0 / 0 | 8 / 662 | 0 / 0 | 15 / 2,343 | 0 / 0 | 2 / 216 |
| power-monoid | 15 / 2,335 | 1 / 43 | 0 / 0 | 0 / 0 | 8 / 760 | 0 / 0 | 8 / 1,483 | 0 / 0 | 0 / 0 |
| power-series-powers-and-roots | 42 / 9,234 | 0 / 0 | 1 / 102 | 0 / 0 | 8 / 662 | 0 / 0 | 15 / 2,343 | 0 / 0 | 2 / 216 |
| power-series-prime-field-helpers | 29 / 5,326 | 0 / 0 | 1 / 102 | 0 / 0 | 7 / 561 | 0 / 0 | 18 / 2,795 | 0 / 0 | 2 / 216 |
| power-series-quick | 39 / 8,830 | 0 / 0 | 2 / 173 | 0 / 0 | 8 / 662 | 0 / 0 | 17 / 2,676 | 0 / 0 | 2 / 216 |
| rmq-basic | 5 / 1,440 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 131 | 0 / 0 | 0 / 0 |
| segment-tree-beats-quick-start | 27 / 8,118 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 13 / 1,204 | 0 / 0 | 0 / 0 |
| segment-tree-quick-start | 9 / 3,062 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 227 | 0 / 0 | 0 / 0 |
| series-composition-quick | 46 / 10,965 | 0 / 0 | 2 / 173 | 0 / 0 | 8 / 662 | 0 / 0 | 15 / 2,343 | 0 / 0 | 2 / 216 |
| sieve-quick | 9 / 1,759 | 1 / 71 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 10 / 1,416 | 0 / 0 | 0 / 0 |
| sparse-table-basic | 2 / 559 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 194 | 0 / 0 | 0 / 0 |
| splay-engine-quick-start | 16 / 5,690 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 144 | 0 / 0 | 5 / 1,084 | 0 / 0 | 0 / 0 |
| splay-map-quick-start | 34 / 11,614 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 3,330 | 0 / 0 | 19 / 7,982 | 0 / 0 | 0 / 0 |
| splay-sequence-quick-start | 39 / 16,368 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 807 | 0 / 0 | 20 / 4,658 | 0 / 0 | 0 / 0 |
| static-vector-basic | 7 / 1,271 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 7 / 701 | 0 / 0 | 0 / 0 |
| subtree-link-cut-tree-basic | 96 / 31,085 | 0 / 0 | 1 / 144 | 0 / 0 | 4 / 1,708 | 0 / 0 | 24 / 7,871 | 0 / 0 | 4 / 508 |
| time-divide-and-conquer-connectivity | 27 / 6,412 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 23 / 6,232 | 0 / 0 | 2 / 248 |
| top-tree-basic | 140 / 44,792 | 0 / 0 | 1 / 77 | 0 / 0 | 1 / 160 | 0 / 0 | 31 / 8,322 | 0 / 0 | 4 / 508 |
| treap-map-quick-start | 35 / 10,207 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 2,778 | 0 / 0 | 16 / 5,301 | 0 / 0 | 0 / 0 |
| treap-sequence-quick-start | 31 / 9,748 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 1,543 | 0 / 0 | 19 / 5,400 | 0 / 0 | 0 / 0 |
| types-and-utilities-composite-key | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| virtual-tree-quick | 7 / 5,226 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 1,090 | 0 / 0 | 4 / 1,526 | 0 / 0 | 0 / 0 |
| virtual-tree-separation | 8 / 5,340 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 1,090 | 0 / 0 | 3 / 1,412 | 0 / 0 | 0 / 0 |
| wavelet-matrix-basic | 12 / 3,100 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 722 | 0 / 0 | 0 / 0 |
| weighted-dsu-matrix | 26 / 4,756 | 1 / 43 | 1 / 102 | 0 / 0 | 6 / 556 | 0 / 0 | 10 / 1,393 | 0 / 0 | 0 / 0 |
| weighted-dsu-potentials | 37 / 7,323 | 1 / 43 | 1 / 102 | 0 / 0 | 6 / 556 | 0 / 0 | 14 / 1,972 | 0 / 0 | 0 / 0 |
| xor-basis-queries | 8 / 1,950 | 1 / 111 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 10 / 1,219 | 0 / 0 | 0 / 0 |

### R-other breakdown

| Independent protection | Units | Bytes |
| --- | ---: | ---: |
| Global pruning refusal (fast-I/O) | 71 | 16,243 |
| Language lookup names | 66 | 7,143 |
| Roots / pins / effects | 23 | 3,033 |

### Per-example top names by class

Names are ranked by owned member bytes; at most five per nonempty class. Empty classes have no names.

| Example | Class | Top names: bytes (units) |
| --- | --- | --- |
| bipartite-matching-dag | R-AST | `build` 1,303 (1); `chains` 358 (1); `paths` 355 (1); `grouped` 301 (1); `current` 232 (1) |
| bipartite-matching-dag | R-lib | `index` 139 (1) |
| bipartite-matching-quick | R-AST | `build` 1,303 (1); `grouped` 301 (1); `current` 232 (1); `out_edges` 205 (1); `initial` 205 (1) |
| bipartite-matching-quick | R-lib | `index` 139 (1) |
| bitwise-convolution-quick | R-AST | `reduce` 652 (2); `add` 644 (2); `mul` 592 (2); `power` 333 (2); `sub` 238 (2) |
| bitwise-convolution-quick | R-dep(c) | `incr` 207 (2); `neg` 204 (2); `decr` 167 (2); `inverse` 101 (1); `mod` 43 (1) |
| bitwise-convolution-quick | R-lib | `mul` 244 (1); `power` 220 (1); `unchecked` 180 (1); `one` 65 (1) |
| centroid-decomposition-quick | R-AST | `adopt` 490 (1); `try_visit` 218 (1); `visited` 138 (1); `vertex_count` 96 (1) |
| centroid-decomposition-quick | R-dep(c) | `build` 1,303 (1); `initial` 205 (1); `out_edges` 205 (1); `exhausted` 149 (1) |
| centroid-decomposition-quick | R-lib | `current` 232 (1); `index` 139 (1); `advance` 135 (1); `pos` 66 (1); `reset` 64 (1) |
| convolution-quick | R-AST | `forward_with` 909 (1); `inverse_with` 886 (1); `reduce` 867 (3); `add` 737 (3); `mul` 673 (3) |
| convolution-quick | R-dep(a) | `from_form` 71 (1) |
| convolution-quick | R-dep(c) | `incr` 207 (2); `neg` 204 (2); `inverse` 182 (2); `decr` 167 (2); `supports` 101 (1) |
| convolution-quick | R-lib | `power` 553 (3); `resize` 426 (1); `mul` 244 (1); `push_back` 218 (1); `emplace_back` 211 (1) |
| convolution-quick | R-other | `end` 114 (1); `begin` 102 (1) |
| coordinate-compression-basic | R-AST | `rank_upper` 195 (1); `rank` 189 (1) |
| coordinate-compression-basic | R-nondep-other | `ranks` 1,987 (1) |
| coordinate-compression-basic | R-lib | `contains` 213 (1); `size` 70 (1); `empty` 65 (1) |
| coordinate-compression-basic | R-other | `begin` 65 (1); `end` 61 (1) |
| coordinate-compression-move-only | R-AST | `rank_upper` 195 (1); `rank` 189 (1) |
| coordinate-compression-move-only | R-nondep-other | `ranks` 1,987 (1) |
| coordinate-compression-move-only | R-lib | `contains` 213 (1); `size` 70 (1); `empty` 65 (1) |
| coordinate-compression-move-only | R-other | `begin` 65 (1); `end` 61 (1) |
| difference-fenwick-basic | R-AST | `differences` 730 (1); `build_tree_layout` 662 (1); `flatten_into` 627 (1); `checked_value` 419 (1); `prefix_fold_impl` 409 (1) |
| difference-fenwick-basic | R-lib | `sizes` 115 (1); `empty` 61 (1) |
| discrete-log-quick | R-AST | `find` 1,438 (2); `insert` 1,330 (1); `mul` 1,233 (6); `find_or_prepare_impl` 925 (1); `reduce` 526 (2) |
| discrete-log-quick | R-dep(c) | `from_form` 144 (2); `to_form` 63 (1) |
| discrete-log-quick | R-lib | `at` 918 (1); `erase` 709 (2); `add` 644 (2); `try_emplace` 604 (1); `contains` 565 (2) |
| discrete-log-quick | R-other | `begin` 350 (3); `end` 114 (1) |
| dsu-history | R-AST | `build` 2,499 (2); `unite` 966 (3); `set` 762 (2); `find` 758 (3); `height_for` 571 (1) |
| dsu-history | R-nondep-other | `same` 194 (2) |
| dsu-history | R-lib | `size` 389 (4); `reserve` 314 (2); `elements` 236 (3); `empty` 51 (1) |
| dual-segment-tree-quick-start | R-AST | `apply` 660 (1); `values` 416 (1); `decompose` 333 (1); `descend_boundaries` 332 (1); `reset` 288 (1) |
| dual-segment-tree-quick-start | R-lib | `at` 228 (1); `size` 58 (1); `empty` 57 (1) |
| dynamic-lazy-segment-tree-quick-start | R-AST | `fold` 973 (1); `apply_rec` 684 (1); `push` 612 (1); `at` 539 (1); `apply` 408 (1) |
| dynamic-lazy-segment-tree-quick-start | R-lib | `reserve` 297 (1); `set` 157 (1); `size` 58 (1); `empty` 57 (1) |
| dynamic-segment-tree-quick-start | R-AST | `point_update` 1,074 (1); `fold` 1,008 (1); `make` 225 (1); `update` 160 (1); `set` 151 (1) |
| dynamic-segment-tree-quick-start | R-lib | `at` 500 (1); `reserve` 263 (1); `size` 58 (1); `empty` 57 (1) |
| euler-tour-tree-basic | R-AST | `assign_parents` 2,078 (1); `visit_segment` 1,423 (1); `insert` 1,330 (1); `insert_before_node` 1,002 (1); `find_or_prepare_impl` 925 (1) |
| euler-tour-tree-basic | R-nondep-other | `split_at` 206 (1) |
| euler-tour-tree-basic | R-dep(c) | `split_at_packed` 492 (1); `split_packed` 481 (1); `iter_at` 160 (1) |
| euler-tour-tree-basic | R-lib | `at` 1,237 (2); `find` 938 (1); `try_emplace` 604 (1); `contains` 565 (2); `insert_or_assign` 382 (1) |
| euler-tour-tree-basic | R-other | `end` 260 (2); `begin` 248 (2) |
| factorization-quick | R-AST | `mul` 673 (3); `reduce` 526 (2); `power` 196 (1); `to_form` 150 (2); `one` 125 (2) |
| factorization-quick | R-nondep-other | `mod` 86 (2); `to_form` 63 (1) |
| factorization-quick | R-lib | `add` 644 (2); `power` 454 (2); `resize` 426 (1); `reduce` 341 (1); `mul` 244 (1) |
| factorization-quick | R-other | `end` 114 (1); `begin` 102 (1) |
| fast-io-roundtrip | R-other | `parse_uint` 1,170 (1); `put` 1,162 (1); `mul` 1,142 (5); `reduce` 867 (3); `read_one` 813 (1) |
| fenwick-tree-basic | R-AST | `build_tree_layout` 662 (1); `flatten_into` 627 (1); `max_right` 464 (1); `prefix_fold_impl` 409 (1); `fill_shape` 404 (1) |
| fenwick-tree-basic | R-lib | `sizes` 115 (1); `size` 94 (1) |
| flat-hash-basic | R-AST | `find_or_prepare_impl` 925 (1); `at` 918 (1); `insert` 700 (1); `find` 500 (1); `rehash` 415 (1) |
| flat-hash-basic | R-dep(c) | `iter_at` 160 (1) |
| flat-hash-basic | R-lib | `find` 1,419 (2); `insert` 1,330 (1); `erase` 1,018 (3); `contains` 881 (3); `count` 680 (2) |
| flat-hash-basic | R-other | `end` 321 (3); `begin` 313 (3) |
| graph-callbacks | R-AST | `try_visit` 376 (2); `current` 232 (1); `initial` 205 (1); `exhausted` 149 (1); `index` 139 (1) |
| graph-callbacks | R-lib | `reset` 64 (1) |
| graph-components-blocks | R-AST | `build` 1,303 (1); `undirected` 502 (1); `adopt` 490 (1); `edge_block` 332 (1); `grouped` 301 (1) |
| graph-components-blocks | R-nondep-other | `grouped` 863 (1) |
| graph-components-blocks | R-lib | `index` 139 (1) |
| graph-components-bridges | R-AST | `build` 1,303 (1); `undirected` 502 (1); `adopt` 490 (1); `grouped` 301 (1); `current` 232 (1) |
| graph-components-bridges | R-lib | `index` 139 (1) |
| graph-components-scc | R-AST | `build` 1,303 (1); `groups` 391 (1); `grouped` 301 (1); `current` 232 (1); `out_edges` 205 (1) |
| graph-components-scc | R-lib | `index` 139 (1) |
| graph-quick | R-AST | `build` 1,303 (1); `seed` 601 (1); `grouped` 301 (1); `current` 232 (1); `try_visit` 218 (1) |
| graph-quick | R-nondep-other | `groups` 391 (1) |
| graph-quick | R-dep(c) | `initial` 205 (1) |
| graph-quick | R-lib | `index` 139 (1); `pos` 66 (1); `reset` 64 (1) |
| hierarchical-bitset-basic | R-AST | `next` 553 (1); `prev` 515 (1); `build_levels` 392 (1); `contains` 136 (1); `min` 71 (1) |
| hierarchical-bitset-basic | R-nondep-other | `universe` 69 (1) |
| hierarchical-bitset-basic | R-lib | `insert` 480 (1); `erase` 297 (1); `clear` 110 (1); `max` 83 (1); `size` 61 (1) |
| lazy-segment-tree-quick-start | R-AST | `apply` 656 (1); `fold` 574 (1); `decompose` 333 (1); `descend_boundaries` 332 (1); `ascend_boundaries` 331 (1) |
| lazy-segment-tree-quick-start | R-lib | `set` 179 (1); `size` 58 (1); `empty` 57 (1) |
| lca-path-sum | R-AST | `build_from_parents` 2,820 (1); `build` 2,002 (2); `path` 1,119 (1); `fold` 444 (1); `path_entry` 351 (1) |
| lca-path-sum | R-dep(c) | `grow` 545 (1); `adopt` 545 (1) |
| lca-path-sum | R-lib | `update` 230 (1); `distance` 183 (1); `set` 133 (1); `parent` 114 (1); `at` 112 (1) |
| lca-quick | R-AST | `build_from_parents` 2,820 (1); `build` 1,683 (1); `path_entry` 351 (1); `path_vertex` 342 (1); `ancestor` 298 (1) |
| lca-quick | R-dep(c) | `grow` 545 (1); `adopt` 545 (1) |
| lca-quick | R-lib | `path` 1,119 (1); `parent` 114 (1); `root` 110 (1); `depth` 109 (1); `order` 83 (1) |
| li-chao-tree-basic | R-AST | `insert` 2,273 (2); `descend` 2,191 (2); `query` 1,349 (2); `insert_segment` 1,238 (1); `push_clip` 1,085 (1) |
| li-chao-tree-basic | R-lib | `reserve` 143 (1); `empty` 101 (2); `size` 52 (1) |
| linear-recurrence-quick | R-AST | `forward_with` 909 (1); `inverse_with` 886 (1); `reduce` 867 (3); `add` 737 (3); `mul` 673 (3) |
| linear-recurrence-quick | R-dep(a) | `from_form` 71 (1) |
| linear-recurrence-quick | R-dep(c) | `row` 312 (1); `incr` 207 (2); `neg` 204 (2); `decr` 167 (2); `is_zero` 167 (2) |
| linear-recurrence-quick | R-lib | `power` 553 (3); `find` 491 (2); `resize` 426 (1); `mul` 244 (1); `push_back` 218 (1) |
| linear-recurrence-quick | R-other | `end` 114 (1); `begin` 102 (1) |
| link-cut-tree-basic | R-AST | `descend` 2,231 (1); `assign` 1,733 (1); `insert` 1,330 (1); `link` 1,238 (1); `find_or_prepare_impl` 925 (1) |
| link-cut-tree-basic | R-dep(a) | `splay_pushed` 144 (1) |
| link-cut-tree-basic | R-dep(c) | `splay_at` 436 (1); `exchange_virtual` 284 (1); `iter_at` 160 (1) |
| link-cut-tree-basic | R-lib | `at` 1,224 (2); `find` 1,057 (2); `for_each` 710 (1); `apply` 643 (1); `try_emplace` 604 (1) |
| link-cut-tree-basic | R-other | `end` 260 (2); `begin` 248 (2) |
| matrix-quick | R-AST | `reduce` 652 (2); `mul` 592 (2); `row_words` 364 (1); `add` 322 (1); `row` 312 (1) |
| matrix-quick | R-dep(c) | `incr` 207 (2); `neg` 204 (2); `is_zero` 167 (2); `decr` 167 (2); `inverse` 101 (1) |
| matrix-quick | R-lib | `power` 553 (3); `add` 386 (2); `find` 288 (1); `mul` 244 (1); `is_unique` 192 (2) |
| meldable-heap-forest-basic | R-AST | `pair_up` 732 (1); `allocate` 563 (1); `erase` 362 (1); `link` 326 (1); `promote` 302 (1) |
| meldable-heap-forest-basic | R-nondep-other | `heap_count` 75 (1) |
| meldable-heap-forest-basic | R-lib | `release` 396 (1); `top` 140 (1); `value` 136 (1); `reserve` 78 (1); `size` 76 (1) |
| modular-arithmetic-residues | R-AST | `mul` 1,142 (5); `reduce` 867 (3); `add` 322 (1); `reduce_lazy` 315 (1); `sub` 238 (2) |
| modular-arithmetic-residues | R-other | `power` 603 (4); `add` 415 (2); `mul_lazy_fixed` 377 (1); `unchecked` 368 (2); `incr` 207 (2) |
| multiplicative-functions-quick | R-AST | `release` 122 (1) |
| multiplicative-functions-quick | R-lib | `resize` 426 (1); `push_back` 218 (1); `emplace_back` 211 (1); `back` 170 (1); `front` 156 (1) |
| multiplicative-functions-quick | R-other | `end` 114 (1); `begin` 102 (1) |
| online-convolution-partitions | R-AST | `schedule` 3,546 (1); `push` 1,008 (2); `forward_with` 909 (1); `inverse_with` 886 (1); `reduce` 867 (3) |
| online-convolution-partitions | R-dep(a) | `from_form` 71 (1) |
| online-convolution-partitions | R-dep(c) | `incr` 207 (2); `neg` 204 (2); `decr` 167 (2); `inverse` 101 (1); `supports` 101 (1) |
| online-convolution-partitions | R-lib | `power` 553 (3); `reserve` 460 (2); `resize` 426 (1); `mul` 244 (1); `push_back` 218 (1) |
| online-convolution-partitions | R-other | `end` 114 (1); `begin` 102 (1) |
| online-convolution-quick | R-AST | `schedule` 3,546 (1); `push` 1,032 (2); `forward_with` 909 (1); `inverse_with` 886 (1); `reduce` 867 (3) |
| online-convolution-quick | R-dep(a) | `from_form` 71 (1) |
| online-convolution-quick | R-dep(c) | `incr` 207 (2); `neg` 204 (2); `inverse` 182 (2); `decr` 167 (2); `supports` 101 (1) |
| online-convolution-quick | R-lib | `power` 553 (3); `reserve` 460 (2); `resize` 426 (1); `mul` 244 (1); `push_back` 218 (1) |
| online-convolution-quick | R-other | `end` 114 (1); `begin` 102 (1) |
| persistent-array-versions | R-AST | `build` 2,935 (3); `set` 1,508 (4); `update_branch` 920 (1); `set_in` 595 (2); `height_for` 571 (1) |
| persistent-array-versions | R-lib | `reserve` 376 (3); `empty` 102 (2); `size` 69 (1) |
| persistent-heap-basic | R-AST | `heapify` 1,497 (1); `push_range_in` 571 (1); `meld` 527 (1); `push_range` 464 (1); `push` 417 (2) |
| persistent-heap-basic | R-dep(c) | `meld` 478 (2); `meld_values` 295 (1) |
| persistent-heap-basic | R-lib | `emplace` 616 (2); `reserve` 356 (2); `right` 270 (2); `left` 268 (2); `push_range` 256 (1) |
| persistent-lazy-segment-tree-basic | R-AST | `copy_range` 1,178 (2); `apply_permanent` 569 (1); `copy_range_in` 438 (1); `apply_in` 430 (1); `build` 347 (1) |
| persistent-lazy-segment-tree-basic | R-dep(c) | `rebuild` 556 (1); `fold` 437 (1); `reserve_extra` 396 (1); `update_in` 281 (1) |
| persistent-lazy-segment-tree-basic | R-lib | `apply` 566 (1); `at` 492 (1); `update` 377 (1); `reserve` 355 (1); `set` 253 (1) |
| persistent-list-basic | R-AST | `push_front` 612 (2); `set` 563 (2); `set_tree` 548 (1); `at` 410 (1); `pop_front` 409 (2) |
| persistent-list-basic | R-dep(c) | `reserve_extra` 396 (1) |
| persistent-list-basic | R-lib | `emplace_front` 434 (1); `reserve` 165 (2); `size` 120 (2); `empty` 50 (1) |
| persistent-list-basic | R-other | `begin` 84 (1); `end` 74 (1) |
| persistent-queue-deque-basic | R-AST | `build` 2,499 (2); `push_front` 836 (3); `set` 762 (2); `height_for` 571 (1); `digit` 562 (1) |
| persistent-queue-deque-basic | R-lib | `emplace_front` 637 (2); `reserve` 601 (5); `set` 563 (2); `pop_front` 409 (2); `emplace` 398 (1) |
| persistent-queue-deque-basic | R-other | `begin` 84 (1); `end` 74 (1) |
| persistent-segment-tree-basic | R-AST | `copy_range` 953 (2); `rebuild` 507 (1); `copy_range_in` 384 (1); `update` 359 (1); `build` 315 (1) |
| persistent-segment-tree-basic | R-dep(c) | `reserve_extra` 396 (1); `fold` 340 (1) |
| persistent-segment-tree-basic | R-lib | `at` 383 (1); `reserve` 254 (1); `set` 235 (1); `size` 69 (1) |
| persistent-treap-sequence-basic | R-AST | `build_tree` 677 (1); `split` 636 (1); `at` 620 (1); `merge_weighted` 550 (1); `fold` 539 (1) |
| persistent-treap-sequence-basic | R-dep(c) | `apply_range` 1,454 (1); `merge_tagged` 1,292 (1); `fold_range` 1,167 (1); `split_at_tagged` 1,142 (1); `split_packed` 481 (1) |
| persistent-treap-sequence-basic | R-lib | `for_each` 858 (1); `split_at_node` 807 (1); `erase` 520 (1); `apply` 506 (1); `merge` 317 (1) |
| persistent-vector-basic | R-AST | `push_leaf` 1,074 (2); `push_back` 1,015 (2); `drop_node` 897 (1); `take_node` 841 (1); `concat_nodes` 780 (1) |
| persistent-vector-basic | R-dep(c) | `build` 1,237 (1); `leaf_at` 657 (1); `height_for` 571 (1); `reserve_extra` 396 (1) |
| persistent-vector-basic | R-lib | `pop_back` 1,494 (4); `push_back` 1,003 (2); `emplace_back` 850 (2); `set` 655 (2); `reserve` 482 (2) |
| persistent-vector-basic | R-other | `end` 91 (1); `begin` 82 (1) |
| polynomial-gcd-pade | R-AST | `forward_with` 909 (1); `inverse_with` 886 (1); `reduce` 867 (3); `add` 737 (3); `mul` 673 (3) |
| polynomial-gcd-pade | R-dep(a) | `neg` 102 (1); `from_form` 71 (1) |
| polynomial-gcd-pade | R-dep(c) | `incr` 207 (2); `decr` 167 (2); `inverse` 101 (1); `supports` 101 (1); `mod` 86 (2) |
| polynomial-gcd-pade | R-lib | `power` 553 (3); `resize` 426 (1); `mul` 244 (1); `push_back` 218 (1); `emplace_back` 211 (1) |
| polynomial-gcd-pade | R-other | `end` 114 (1); `begin` 102 (1) |
| polynomial-gcd-quick | R-AST | `forward_with` 909 (1); `inverse_with` 886 (1); `reduce` 867 (3); `add` 737 (3); `mul` 673 (3) |
| polynomial-gcd-quick | R-dep(a) | `neg` 102 (1); `from_form` 71 (1) |
| polynomial-gcd-quick | R-dep(c) | `incr` 207 (2); `decr` 167 (2); `inverse` 101 (1); `supports` 101 (1); `mod` 86 (2) |
| polynomial-gcd-quick | R-lib | `power` 553 (3); `resize` 426 (1); `mul` 244 (1); `push_back` 218 (1); `emplace_back` 211 (1) |
| polynomial-gcd-quick | R-other | `end` 114 (1); `begin` 102 (1) |
| polynomial-points-quick | R-AST | `descend` 1,767 (1); `build` 1,525 (1); `ascend` 1,443 (1); `evaluate` 1,365 (2); `extend` 1,268 (1) |
| polynomial-points-quick | R-nondep-other | `points` 155 (1) |
| polynomial-points-quick | R-dep(a) | `neg` 102 (1); `from_form` 71 (1) |
| polynomial-points-quick | R-dep(c) | `incr` 207 (2); `decr` 167 (2); `inverse` 101 (1); `supports` 101 (1); `mod` 86 (2) |
| polynomial-points-quick | R-lib | `power` 553 (3); `resize` 426 (1); `mul` 244 (1); `push_back` 218 (1); `emplace_back` 211 (1) |
| polynomial-points-quick | R-other | `end` 114 (1); `begin` 102 (1) |
| polynomial-points-sampling | R-AST | `extend` 1,268 (1); `forward_with` 909 (1); `inverse_with` 886 (1); `reduce` 867 (3); `add` 737 (3) |
| polynomial-points-sampling | R-dep(a) | `neg` 102 (1); `from_form` 71 (1) |
| polynomial-points-sampling | R-dep(c) | `incr` 207 (2); `decr` 167 (2); `inverse` 101 (1); `supports` 101 (1); `mod` 86 (2) |
| polynomial-points-sampling | R-lib | `resize` 426 (1); `mul` 244 (1); `power` 220 (1); `push_back` 218 (1); `emplace_back` 211 (1) |
| polynomial-points-sampling | R-other | `end` 114 (1); `begin` 102 (1) |
| power-monoid | R-AST | `reduce` 652 (2); `mul` 592 (2); `add` 322 (1); `from_form` 144 (2); `to_form` 142 (2) |
| power-monoid | R-nondep-other | `mod` 43 (1) |
| power-monoid | R-dep(c) | `incr` 207 (2); `neg` 204 (2); `inverse` 182 (2); `decr` 167 (2) |
| power-monoid | R-lib | `power` 553 (3); `add` 322 (1); `mul` 244 (1); `unchecked` 180 (1); `sub` 119 (1) |
| power-series-powers-and-roots | R-AST | `forward_with` 909 (1); `inverse_with` 886 (1); `reduce` 867 (3); `add` 737 (3); `mul` 673 (3) |
| power-series-powers-and-roots | R-dep(a) | `neg` 102 (1) |
| power-series-powers-and-roots | R-dep(c) | `incr` 207 (2); `decr` 167 (2); `inverse` 101 (1); `supports` 101 (1); `mod` 86 (2) |
| power-series-powers-and-roots | R-lib | `resize` 426 (1); `mul` 244 (1); `power` 220 (1); `push_back` 218 (1); `emplace_back` 211 (1) |
| power-series-powers-and-roots | R-other | `end` 114 (1); `begin` 102 (1) |
| power-series-prime-field-helpers | R-AST | `extend` 1,268 (1); `reduce` 867 (3); `add` 737 (3); `mul` 673 (3); `binomial` 240 (1) |
| power-series-prime-field-helpers | R-dep(a) | `neg` 102 (1) |
| power-series-prime-field-helpers | R-dep(c) | `incr` 207 (2); `decr` 167 (2); `inverse` 101 (1); `mod` 86 (2) |
| power-series-prime-field-helpers | R-lib | `power` 553 (3); `resize` 426 (1); `mul` 244 (1); `push_back` 218 (1); `emplace_back` 211 (1) |
| power-series-prime-field-helpers | R-other | `end` 114 (1); `begin` 102 (1) |
| power-series-quick | R-AST | `forward_with` 909 (1); `inverse_with` 886 (1); `reduce` 867 (3); `add` 737 (3); `mul` 673 (3) |
| power-series-quick | R-dep(a) | `neg` 102 (1); `from_form` 71 (1) |
| power-series-quick | R-dep(c) | `incr` 207 (2); `decr` 167 (2); `inverse` 101 (1); `supports` 101 (1); `mod` 86 (2) |
| power-series-quick | R-lib | `power` 553 (3); `resize` 426 (1); `mul` 244 (1); `push_back` 218 (1); `emplace_back` 211 (1) |
| power-series-quick | R-other | `end` 114 (1); `begin` 102 (1) |
| rmq-basic | R-AST | `position` 757 (1); `in_block_query` 347 (1); `checked_size` 133 (1); `argmin` 110 (1); `fold` 93 (1) |
| rmq-basic | R-lib | `size` 68 (1); `empty` 63 (1) |
| segment-tree-beats-quick-start | R-AST | `pull` 948 (1); `shift` 709 (1); `push_down` 592 (1); `reset` 440 (2); `lower_max` 377 (1) |
| segment-tree-beats-quick-start | R-lib | `identity` 366 (5); `of` 271 (3); `set` 177 (1); `element` 140 (1); `at` 135 (1) |
| segment-tree-quick-start | R-AST | `max_right` 1,038 (1); `fold` 444 (1); `decompose` 333 (1); `build` 319 (1); `reset` 288 (1) |
| segment-tree-quick-start | R-lib | `at` 112 (1); `size` 58 (1); `empty` 57 (1) |
| series-composition-quick | R-AST | `extend` 1,268 (1); `forward_with` 909 (1); `inverse_with` 886 (1); `reduce` 867 (3); `add` 737 (3) |
| series-composition-quick | R-dep(a) | `neg` 102 (1); `from_form` 71 (1) |
| series-composition-quick | R-dep(c) | `incr` 207 (2); `decr` 167 (2); `inverse` 101 (1); `supports` 101 (1); `mod` 86 (2) |
| series-composition-quick | R-lib | `resize` 426 (1); `mul` 244 (1); `power` 220 (1); `push_back` 218 (1); `emplace_back` 211 (1) |
| series-composition-quick | R-other | `end` 114 (1); `begin` 102 (1) |
| sieve-quick | R-AST | `divisors` 480 (1); `factorization` 395 (1); `push_back` 218 (1); `min_prime_factor` 137 (1); `is_prime` 132 (1) |
| sieve-quick | R-nondep-other | `primes` 71 (1) |
| sieve-quick | R-lib | `resize` 426 (1); `emplace_back` 211 (1); `back` 170 (1); `front` 156 (1); `data` 118 (1) |
| sparse-table-basic | R-AST | `fold` 559 (2) |
| sparse-table-basic | R-lib | `size` 98 (2); `empty` 96 (2) |
| splay-engine-quick-start | R-AST | `descend` 2,231 (1); `for_each` 710 (1); `splay_at` 436 (1); `split_at` 338 (1); `join` 335 (1) |
| splay-engine-quick-start | R-dep(c) | `splay_pushed` 144 (1) |
| splay-engine-quick-start | R-lib | `erase` 375 (1); `rotate` 270 (1); `reserve` 193 (1); `splay` 171 (1); `value` 75 (1) |
| splay-map-quick-start | R-AST | `descend` 2,231 (1); `fold` 780 (1); `isolate` 741 (1); `update` 580 (1); `recycle` 545 (1) |
| splay-map-quick-start | R-dep(c) | `build_balanced` 1,300 (1); `build_sorted` 935 (1); `splay_at` 436 (1); `visit_node` 364 (1); `free_node` 151 (1) |
| splay-map-quick-start | R-lib | `for_each` 1,717 (2); `erase` 1,542 (2); `next` 624 (1); `prev` 622 (1); `reserve` 522 (2) |
| splay-sequence-quick-start | R-AST | `descend` 2,231 (1); `build_balanced` 1,300 (1); `visit_values` 1,245 (1); `append_node` 797 (1); `make` 768 (1) |
| splay-sequence-quick-start | R-dep(c) | `map_value` 354 (1); `compose_tags` 309 (1); `splay_pushed` 144 (1) |
| splay-sequence-quick-start | R-lib | `erase` 793 (2); `for_each` 710 (1); `join` 335 (1); `apply` 321 (1); `at` 302 (1) |
| static-vector-basic | R-AST | `resize` 426 (1); `push_back` 218 (1); `emplace_back` 211 (1); `release` 122 (1); `end` 114 (1) |
| static-vector-basic | R-lib | `back` 170 (1); `front` 156 (1); `data` 118 (1); `capacity` 69 (1); `clear` 67 (1) |
| subtree-link-cut-tree-basic | R-AST | `descend` 2,231 (1); `assign` 1,733 (1); `assign_parents` 1,585 (1); `insert` 1,330 (1); `link` 1,238 (1) |
| subtree-link-cut-tree-basic | R-dep(a) | `splay_pushed` 144 (1) |
| subtree-link-cut-tree-basic | R-dep(c) | `fold_virtual_children` 804 (1); `splay_at` 436 (1); `is_forest` 308 (1); `iter_at` 160 (1) |
| subtree-link-cut-tree-basic | R-lib | `find` 1,057 (2); `at` 918 (1); `for_each` 710 (1); `apply` 643 (1); `try_emplace` 604 (1) |
| subtree-link-cut-tree-basic | R-other | `end` 260 (2); `begin` 248 (2) |
| time-divide-and-conquer-connectivity | R-AST | `find` 1,633 (3); `find_or_prepare_impl` 925 (1); `rehash` 415 (1); `unite` 410 (1); `home` 316 (1) |
| time-divide-and-conquer-connectivity | R-lib | `insert` 1,330 (1); `at` 918 (1); `erase` 709 (2); `try_emplace` 604 (1); `contains` 565 (2) |
| time-divide-and-conquer-connectivity | R-other | `begin` 248 (2) |
| top-tree-basic | R-AST | `descend` 2,231 (1); `build_rake_trees` 1,419 (1); `assign_parents` 1,337 (1); `insert` 1,330 (1); `build_balanced` 1,300 (1) |
| top-tree-basic | R-dep(a) | `add_of` 77 (1) |
| top-tree-basic | R-dep(c) | `iter_at` 160 (1) |
| top-tree-basic | R-lib | `at` 1,262 (2); `find` 1,057 (2); `for_each` 710 (1); `try_emplace` 604 (1); `contains` 565 (2) |
| top-tree-basic | R-other | `end` 260 (2); `begin` 248 (2) |
| treap-map-quick-start | R-AST | `upsert` 1,234 (1); `fold` 879 (1); `insert` 722 (1); `make` 548 (1); `split_packed` 481 (1) |
| treap-map-quick-start | R-dep(c) | `build_sorted` 1,353 (1); `visit_range` 740 (1); `visit_subtree` 442 (1); `split_key_inclusive` 243 (1) |
| treap-map-quick-start | R-lib | `erase` 881 (1); `split_at_node` 807 (1); `for_each` 692 (1); `next` 479 (1); `prev` 479 (1) |
| treap-sequence-quick-start | R-AST | `fold_range` 1,667 (1); `make` 858 (1); `build_tree` 813 (1); `visit_subtree` 762 (1); `push` 546 (1) |
| treap-sequence-quick-start | R-dep(c) | `split_packed` 481 (1); `apply_tag` 399 (1); `map_value` 354 (1); `compose_tags` 309 (1) |
| treap-sequence-quick-start | R-lib | `at` 1,225 (1); `split_at_node` 807 (1); `clone` 583 (1); `split` 474 (2); `set` 319 (1) |
| virtual-tree-quick | R-AST | `build_from_parents` 2,820 (1); `build` 1,683 (1); `path_entry` 351 (1); `entry_time` 119 (1); `depth` 109 (1) |
| virtual-tree-quick | R-dep(c) | `grow` 545 (1); `adopt` 545 (1) |
| virtual-tree-quick | R-lib | `path` 1,119 (1); `distance` 183 (1); `parent` 114 (1); `root` 110 (1) |
| virtual-tree-separation | R-AST | `build_from_parents` 2,820 (1); `build` 1,683 (1); `path_entry` 351 (1); `entry_time` 119 (1); `parent` 114 (1) |
| virtual-tree-separation | R-dep(c) | `grow` 545 (1); `adopt` 545 (1) |
| virtual-tree-separation | R-lib | `path` 1,119 (1); `distance` 183 (1); `root` 110 (1) |
| wavelet-matrix-basic | R-AST | `count_less` 588 (1); `kth` 532 (1); `fits` 408 (1); `value_width` 292 (1); `finish` 282 (1) |
| wavelet-matrix-basic | R-lib | `at` 443 (2); `size` 116 (2); `set` 106 (1); `empty` 57 (1) |
| weighted-dsu-matrix | R-AST | `reduce` 652 (2); `unite` 644 (1); `add` 644 (2); `mul` 592 (2); `compress` 257 (1) |
| weighted-dsu-matrix | R-nondep-other | `mod` 43 (1) |
| weighted-dsu-matrix | R-dep(a) | `neg` 102 (1) |
| weighted-dsu-matrix | R-dep(c) | `incr` 207 (2); `inverse` 182 (2); `decr` 167 (2) |
| weighted-dsu-matrix | R-lib | `power` 553 (3); `mul` 244 (1); `unchecked` 180 (1); `sub` 119 (1); `size` 95 (1) |
| weighted-dsu-potentials | R-AST | `unite` 1,333 (2); `add` 644 (2); `mul` 592 (2); `locate` 534 (2); `difference` 514 (2) |
| weighted-dsu-potentials | R-nondep-other | `mod` 43 (1) |
| weighted-dsu-potentials | R-dep(a) | `neg` 102 (1) |
| weighted-dsu-potentials | R-dep(c) | `incr` 207 (2); `inverse` 182 (2); `decr` 167 (2) |
| weighted-dsu-potentials | R-lib | `power` 553 (3); `reduce` 341 (1); `mul` 244 (1); `size` 196 (2); `unchecked` 180 (1) |
| xor-basis-queries | R-AST | `insert` 872 (2); `max_xor` 347 (2); `kth_smallest` 308 (1); `combination` 210 (1); `representable_count` 116 (1) |
| xor-basis-queries | R-nondep-other | `stamp` 111 (1) |
| xor-basis-queries | R-lib | `rank` 315 (2); `contains` 311 (2); `merge` 212 (1); `vector` 210 (2); `clear` 111 (2) |

### Dependent outcomes per example

(b) is an overlapping positive observation already included in R-AST. Cells are units / bytes.

| Example | (a) only other known targets | (b) some target this unit | (c) unresolved sites |
| --- | ---: | ---: | ---: |
| assignment-quick | 0 / 0 | 0 / 0 | 0 / 0 |
| bipartite-matching-dag | 0 / 0 | 8 / 2,391 | 0 / 0 |
| bipartite-matching-quick | 0 / 0 | 8 / 2,391 | 0 / 0 |
| bit-utilities-combinations | 0 / 0 | 0 / 0 | 0 / 0 |
| bitwise-convolution-quick | 0 / 0 | 11 / 1,948 | 8 / 722 |
| centroid-decomposition-quick | 0 / 0 | 1 / 490 | 4 / 1,862 |
| convolution-quick | 1 / 71 | 21 / 6,325 | 11 / 947 |
| coordinate-compression-basic | 0 / 0 | 0 / 0 | 0 / 0 |
| coordinate-compression-move-only | 0 / 0 | 0 / 0 | 0 / 0 |
| counting-sort-stable-records | 0 / 0 | 0 / 0 | 0 / 0 |
| crt-quick | 0 / 0 | 0 / 0 | 0 / 0 |
| difference-fenwick-basic | 0 / 0 | 9 / 3,767 | 0 / 0 |
| discrete-log-quick | 0 / 0 | 22 / 3,880 | 3 / 207 |
| dsu-history | 0 / 0 | 11 / 5,374 | 0 / 0 |
| dual-segment-tree-quick-start | 0 / 0 | 4 / 1,031 | 0 / 0 |
| dynamic-lazy-segment-tree-quick-start | 0 / 0 | 0 / 0 | 0 / 0 |
| dynamic-segment-tree-quick-start | 0 / 0 | 1 / 1,074 | 0 / 0 |
| euler-tour-tree-basic | 0 / 0 | 29 / 9,816 | 3 / 1,133 |
| factorization-quick | 0 / 0 | 0 / 0 | 0 / 0 |
| fast-io-roundtrip | 0 / 0 | 0 / 0 | 0 / 0 |
| fenwick-tree-basic | 0 / 0 | 4 / 1,842 | 0 / 0 |
| flat-hash-basic | 0 / 0 | 5 / 2,089 | 1 / 160 |
| floor-sum-quick | 0 / 0 | 0 / 0 | 0 / 0 |
| gcd-bezout | 0 / 0 | 0 / 0 | 0 / 0 |
| graph-callbacks | 0 / 0 | 8 / 1,302 | 0 / 0 |
| graph-components-blocks | 0 / 0 | 9 / 2,893 | 0 / 0 |
| graph-components-bridges | 0 / 0 | 9 / 2,893 | 0 / 0 |
| graph-components-scc | 0 / 0 | 8 / 2,391 | 0 / 0 |
| graph-quick | 0 / 0 | 8 / 2,476 | 1 / 205 |
| hierarchical-bitset-basic | 0 / 0 | 0 / 0 | 0 / 0 |
| int128-arithmetic | 0 / 0 | 0 / 0 | 0 / 0 |
| lazy-segment-tree-quick-start | 0 / 0 | 7 / 1,880 | 0 / 0 |
| lca-path-sum | 0 / 0 | 5 / 1,217 | 2 / 1,090 |
| lca-quick | 0 / 0 | 0 / 0 | 2 / 1,090 |
| li-chao-tree-basic | 0 / 0 | 0 / 0 | 0 / 0 |
| linear-recurrence-quick | 1 / 71 | 24 / 6,613 | 16 / 1,514 |
| link-cut-tree-basic | 1 / 144 | 26 / 11,015 | 3 / 880 |
| matrix-quick | 0 / 0 | 16 / 2,316 | 11 / 958 |
| max-flow-bounds | 0 / 0 | 0 / 0 | 0 / 0 |
| max-flow-quick | 0 / 0 | 0 / 0 | 0 / 0 |
| meldable-heap-forest-basic | 0 / 0 | 4 / 1,220 | 0 / 0 |
| min-cost-flow-quick | 0 / 0 | 0 / 0 | 0 / 0 |
| min-cost-flow-supplies | 0 / 0 | 0 / 0 | 0 / 0 |
| modular-arithmetic-residues | 0 / 0 | 7 / 1,364 | 0 / 0 |
| multiplicative-functions-quick | 0 / 0 | 0 / 0 | 0 / 0 |
| online-convolution-partitions | 1 / 71 | 23 / 7,324 | 10 / 866 |
| online-convolution-quick | 1 / 71 | 22 / 7,243 | 11 / 947 |
| persistent-array-versions | 0 / 0 | 12 / 6,246 | 0 / 0 |
| persistent-heap-basic | 0 / 0 | 9 / 3,617 | 3 / 773 |
| persistent-lazy-segment-tree-basic | 0 / 0 | 4 / 1,831 | 4 / 1,670 |
| persistent-list-basic | 0 / 0 | 7 / 1,798 | 1 / 396 |
| persistent-queue-deque-basic | 0 / 0 | 16 / 6,617 | 0 / 0 |
| persistent-segment-tree-basic | 0 / 0 | 5 / 1,804 | 2 / 736 |
| persistent-treap-sequence-basic | 0 / 0 | 18 / 4,826 | 10 / 7,045 |
| persistent-vector-basic | 0 / 0 | 12 / 4,061 | 4 / 2,861 |
| polynomial-gcd-pade | 2 / 173 | 24 / 6,627 | 8 / 662 |
| polynomial-gcd-quick | 2 / 173 | 24 / 6,627 | 8 / 662 |
| polynomial-points-quick | 2 / 173 | 29 / 8,836 | 9 / 735 |
| polynomial-points-sampling | 2 / 173 | 28 / 7,199 | 8 / 662 |
| power-monoid | 0 / 0 | 4 / 958 | 8 / 760 |
| power-series-powers-and-roots | 1 / 102 | 26 / 6,960 | 8 / 662 |
| power-series-prime-field-helpers | 1 / 102 | 10 / 1,662 | 7 / 561 |
| power-series-quick | 2 / 173 | 24 / 6,627 | 8 / 662 |
| rmq-basic | 0 / 0 | 0 / 0 | 0 / 0 |
| segment-tree-beats-quick-start | 0 / 0 | 10 / 2,617 | 0 / 0 |
| segment-tree-quick-start | 0 / 0 | 5 / 1,217 | 0 / 0 |
| series-composition-quick | 2 / 173 | 30 / 7,504 | 8 / 662 |
| sieve-quick | 0 / 0 | 0 / 0 | 0 / 0 |
| sparse-table-basic | 0 / 0 | 0 / 0 | 0 / 0 |
| splay-engine-quick-start | 0 / 0 | 10 / 3,784 | 1 / 144 |
| splay-map-quick-start | 0 / 0 | 21 / 7,418 | 6 / 3,330 |
| splay-sequence-quick-start | 0 / 0 | 22 / 11,548 | 3 / 807 |
| static-vector-basic | 0 / 0 | 1 / 218 | 0 / 0 |
| subtree-link-cut-tree-basic | 1 / 144 | 52 / 20,042 | 4 / 1,708 |
| time-divide-and-conquer-connectivity | 0 / 0 | 14 / 3,899 | 0 / 0 |
| top-tree-basic | 1 / 77 | 61 / 21,021 | 1 / 160 |
| treap-map-quick-start | 0 / 0 | 24 / 7,267 | 4 / 2,778 |
| treap-sequence-quick-start | 0 / 0 | 20 / 7,890 | 4 / 1,543 |
| types-and-utilities-composite-key | 0 / 0 | 0 / 0 | 0 / 0 |
| virtual-tree-quick | 0 / 0 | 0 / 0 | 2 / 1,090 |
| virtual-tree-separation | 0 / 0 | 0 / 0 | 2 / 1,090 |
| wavelet-matrix-basic | 0 / 0 | 2 / 619 | 0 / 0 |
| weighted-dsu-matrix | 1 / 102 | 6 / 1,382 | 6 / 556 |
| weighted-dsu-potentials | 1 / 102 | 5 / 871 | 6 / 556 |
| xor-basis-queries | 0 / 0 | 0 / 0 | 0 / 0 |

### Dependent (b) top names per example

| Example | Top names: bytes (units), overlapping R-AST |
| --- | --- |
| bipartite-matching-dag | `build` 1,303 (1); `current` 232 (1); `out_edges` 205 (1); `initial` 205 (1); `exhausted` 149 (1) |
| bipartite-matching-quick | `build` 1,303 (1); `current` 232 (1); `out_edges` 205 (1); `initial` 205 (1); `exhausted` 149 (1) |
| bitwise-convolution-quick | `mul` 489 (1); `reduce` 341 (1); `power` 333 (2); `add` 322 (1); `inverse` 169 (2) |
| centroid-decomposition-quick | `adopt` 490 (1) |
| convolution-quick | `forward_with` 909 (1); `inverse_with` 886 (1); `mul` 592 (2); `scale_down` 555 (1); `convolve` 542 (1) |
| difference-fenwick-basic | `differences` 730 (1); `flatten_into` 627 (1); `checked_value` 419 (1); `prefix_fold_impl` 409 (1); `fill_shape` 404 (1) |
| discrete-log-quick | `find_or_prepare_impl` 925 (1); `mul` 641 (4); `find` 500 (1); `home` 316 (1); `log` 294 (1) |
| dsu-history | `build` 2,499 (2); `set` 762 (2); `height_for` 571 (1); `digit` 562 (1); `reserve_extra` 396 (1) |
| dual-segment-tree-quick-start | `decompose` 333 (1); `descend_boundaries` 332 (1); `reset` 288 (1); `leaf` 78 (1) |
| dynamic-segment-tree-quick-start | `point_update` 1,074 (1) |
| euler-tour-tree-basic | `visit_segment` 1,423 (1); `insert_before_node` 1,002 (1); `find_or_prepare_impl` 925 (1); `split_at_node` 807 (1); `assign` 561 (1) |
| fenwick-tree-basic | `flatten_into` 627 (1); `prefix_fold_impl` 409 (1); `fill_shape` 404 (1); `update_impl` 402 (1) |
| flat-hash-basic | `find_or_prepare_impl` 925 (1); `find` 500 (1); `home` 316 (1); `find_or_prepare` 222 (1); `slot` 126 (1) |
| graph-callbacks | `try_visit` 376 (2); `current` 232 (1); `initial` 205 (1); `exhausted` 149 (1); `index` 139 (1) |
| graph-components-blocks | `build` 1,303 (1); `undirected` 502 (1); `current` 232 (1); `initial` 205 (1); `out_edges` 205 (1) |
| graph-components-bridges | `build` 1,303 (1); `undirected` 502 (1); `current` 232 (1); `out_edges` 205 (1); `initial` 205 (1) |
| graph-components-scc | `build` 1,303 (1); `current` 232 (1); `out_edges` 205 (1); `initial` 205 (1); `exhausted` 149 (1) |
| graph-quick | `build` 1,303 (1); `current` 232 (1); `try_visit` 218 (1); `out_edges` 205 (1); `exhausted` 149 (1) |
| lazy-segment-tree-quick-start | `decompose` 333 (1); `descend_boundaries` 332 (1); `ascend_boundaries` 331 (1); `build` 319 (1); `reset` 288 (1) |
| lca-path-sum | `decompose` 333 (1); `build` 319 (1); `reset` 288 (1); `recompute_up` 199 (1); `leaf` 78 (1) |
| linear-recurrence-quick | `forward_with` 909 (1); `inverse_with` 886 (1); `mul` 592 (2); `scale_down` 555 (1); `convolve` 542 (1) |
| link-cut-tree-basic | `descend` 2,231 (1); `assign` 1,733 (1); `find_or_prepare_impl` 925 (1); `pull` 603 (1); `push` 584 (1) |
| matrix-quick | `mul` 489 (1); `reduce` 341 (1); `row` 312 (1); `find` 203 (1); `unchecked` 180 (1) |
| meldable-heap-forest-basic | `allocate` 563 (1); `emplace` 299 (1); `record` 238 (1); `at` 120 (1) |
| modular-arithmetic-residues | `mul` 733 (2); `reduce` 341 (1); `sub` 119 (1); `from_form` 65 (1); `to_form` 63 (1) |
| online-convolution-partitions | `push` 918 (1); `forward_with` 909 (1); `inverse_with` 886 (1); `mul` 592 (2); `scale_down` 555 (1) |
| online-convolution-quick | `push` 918 (1); `forward_with` 909 (1); `inverse_with` 886 (1); `mul` 592 (2); `scale_down` 555 (1) |
| persistent-array-versions | `build` 2,935 (3); `set` 982 (2); `height_for` 571 (1); `digit` 562 (1); `reserve_extra` 396 (1) |
| persistent-heap-basic | `heapify` 1,497 (1); `push_range_in` 571 (1); `meld` 527 (1); `reserve_extra` 396 (1); `make` 179 (1) |
| persistent-lazy-segment-tree-basic | `copy_range` 736 (1); `apply_permanent` 569 (1); `build` 347 (1); `make` 179 (1) |
| persistent-list-basic | `at` 410 (1); `push_front` 390 (1); `set` 333 (1); `pop_front` 241 (1); `make` 179 (1) |
| persistent-queue-deque-basic | `build` 2,499 (2); `set` 762 (2); `push_front` 612 (2); `height_for` 571 (1); `digit` 562 (1) |
| persistent-segment-tree-basic | `copy_range` 531 (1); `rebuild` 507 (1); `build` 315 (1); `update_in` 272 (1); `make` 179 (1) |
| persistent-treap-sequence-basic | `build_tree` 677 (1); `merge_weighted` 550 (1); `split_at_packed` 492 (1); `push` 427 (1); `merge_spines` 414 (1) |
| persistent-vector-basic | `push_back` 783 (1); `trie_build` 586 (1); `digit` 562 (1); `at` 557 (2); `set` 385 (1) |
| polynomial-gcd-pade | `forward_with` 909 (1); `inverse_with` 886 (1); `mul` 592 (2); `scale_down` 555 (1); `convolve` 542 (1) |
| polynomial-gcd-quick | `forward_with` 909 (1); `inverse_with` 886 (1); `mul` 592 (2); `scale_down` 555 (1); `convolve` 542 (1) |
| polynomial-points-quick | `evaluate` 1,089 (1); `forward_with` 909 (1); `inverse_with` 886 (1); `interpolate` 746 (1); `mul` 592 (2) |
| polynomial-points-sampling | `forward_with` 909 (1); `inverse_with` 886 (1); `mul` 592 (2); `scale_down` 555 (1); `convolve` 542 (1) |
| power-monoid | `mul` 489 (1); `reduce` 341 (1); `from_form` 65 (1); `to_form` 63 (1) |
| power-series-powers-and-roots | `forward_with` 909 (1); `inverse_with` 886 (1); `mul` 592 (2); `scale_down` 555 (1); `convolve` 542 (1) |
| power-series-prime-field-helpers | `mul` 489 (1); `reduce` 341 (1); `add` 322 (1); `inverse` 169 (2); `neg` 102 (1) |
| power-series-quick | `forward_with` 909 (1); `inverse_with` 886 (1); `mul` 592 (2); `scale_down` 555 (1); `convolve` 542 (1) |
| segment-tree-beats-quick-start | `update` 343 (1); `decompose` 333 (1); `descend_boundaries` 332 (1); `ascend_boundaries` 331 (1); `build` 319 (1) |
| segment-tree-quick-start | `decompose` 333 (1); `build` 319 (1); `reset` 288 (1); `recompute_up` 199 (1); `leaf` 78 (1) |
| series-composition-quick | `forward_with` 909 (1); `inverse_with` 886 (1); `mul` 592 (2); `scale_down` 555 (1); `convolve` 542 (1) |
| splay-engine-quick-start | `descend` 2,231 (1); `splay_at` 436 (1); `push` 306 (1); `detach` 253 (1); `pull` 118 (1) |
| splay-map-quick-start | `descend` 2,231 (1); `isolate` 741 (1); `recycle` 545 (1); `make` 477 (1); `splay_exact` 419 (1) |
| splay-sequence-quick-start | `descend` 2,231 (1); `build_balanced` 1,300 (1); `visit_values` 1,245 (1); `make` 768 (1); `recycle` 673 (1) |
| static-vector-basic | `push_back` 218 (1) |
| subtree-link-cut-tree-basic | `descend` 2,231 (1); `assign` 1,733 (1); `assign_parents` 1,585 (1); `link` 1,238 (1); `find_or_prepare_impl` 925 (1) |
| time-divide-and-conquer-connectivity | `find` 1,438 (2); `find_or_prepare_impl` 925 (1); `home` 316 (1); `end` 260 (2); `find_or_prepare` 222 (1) |
| top-tree-basic | `descend` 2,231 (1); `build_balanced` 1,300 (1); `init` 1,173 (1); `find_or_prepare_impl` 925 (1); `subtree_apply` 871 (1) |
| treap-map-quick-start | `upsert` 1,234 (1); `insert` 722 (1); `make` 548 (1); `split_packed` 481 (1); `fold_until` 419 (1) |
| treap-sequence-quick-start | `fold_range` 1,667 (1); `make` 858 (1); `build_tree` 813 (1); `visit_subtree` 762 (1); `push` 546 (1) |
| wavelet-matrix-basic | `fits` 408 (1); `checked_size` 211 (1) |
| weighted-dsu-matrix | `mul` 489 (1); `reduce` 341 (1); `add` 322 (1); `neg` 102 (1); `from_form` 65 (1) |
| weighted-dsu-potentials | `add` 322 (1); `link` 319 (1); `neg` 102 (1); `from_form` 65 (1); `to_form` 63 (1) |

### Units retained only by locally unrelated bindings

These rows lack independent inventory/root protection and are measurement candidates, not all-target removal proofs.

| Example | Class | Unit | Bytes |
| --- | --- | --- | ---: |
| convolution-quick | R-dep(a) | `atl::detail::montgomery64::from_form` | 71 |
| coordinate-compression-basic | R-nondep-other | `atl::coordinate_compression::ranks` | 1,987 |
| coordinate-compression-move-only | R-nondep-other | `atl::coordinate_compression::ranks` | 1,987 |
| dsu-history | R-nondep-other | `atl::dsu::same` | 94 |
| dsu-history | R-nondep-other | `atl::rollback_dsu::same` | 100 |
| euler-tour-tree-basic | R-nondep-other | `atl::detail::treap_base::split_at` | 206 |
| factorization-quick | R-nondep-other | `atl::barrett64::mod` | 43 |
| factorization-quick | R-nondep-other | `atl::static_modulus::to_form` | 63 |
| factorization-quick | R-nondep-other | `atl::detail::montgomery64::mod` | 43 |
| graph-components-blocks | R-nondep-other | `atl::graph::block_decomposition::grouped` | 863 |
| graph-quick | R-nondep-other | `atl::graph::vertex_partition::groups` | 391 |
| hierarchical-bitset-basic | R-nondep-other | `atl::hierarchical_bitset::universe` | 69 |
| linear-recurrence-quick | R-dep(a) | `atl::detail::montgomery64::from_form` | 71 |
| link-cut-tree-basic | R-dep(a) | `atl::splay_engine::splay_pushed` | 144 |
| meldable-heap-forest-basic | R-nondep-other | `atl::detail::heap_directory::heap_count` | 75 |
| online-convolution-partitions | R-dep(a) | `atl::detail::montgomery64::from_form` | 71 |
| online-convolution-quick | R-dep(a) | `atl::detail::montgomery64::from_form` | 71 |
| polynomial-gcd-pade | R-dep(a) | `atl::montgomery::neg` | 102 |
| polynomial-gcd-pade | R-dep(a) | `atl::detail::montgomery64::from_form` | 71 |
| polynomial-gcd-quick | R-dep(a) | `atl::montgomery::neg` | 102 |
| polynomial-gcd-quick | R-dep(a) | `atl::detail::montgomery64::from_form` | 71 |
| polynomial-points-quick | R-dep(a) | `atl::montgomery::neg` | 102 |
| polynomial-points-quick | R-dep(a) | `atl::detail::montgomery64::from_form` | 71 |
| polynomial-points-quick | R-nondep-other | `atl::polynomial_point_tree::points` | 155 |
| polynomial-points-sampling | R-dep(a) | `atl::montgomery::neg` | 102 |
| polynomial-points-sampling | R-dep(a) | `atl::detail::montgomery64::from_form` | 71 |
| power-monoid | R-nondep-other | `atl::barrett64::mod` | 43 |
| power-series-powers-and-roots | R-dep(a) | `atl::montgomery::neg` | 102 |
| power-series-prime-field-helpers | R-dep(a) | `atl::montgomery::neg` | 102 |
| power-series-quick | R-dep(a) | `atl::montgomery::neg` | 102 |
| power-series-quick | R-dep(a) | `atl::detail::montgomery64::from_form` | 71 |
| series-composition-quick | R-dep(a) | `atl::montgomery::neg` | 102 |
| series-composition-quick | R-dep(a) | `atl::detail::montgomery64::from_form` | 71 |
| sieve-quick | R-nondep-other | `atl::sieve::primes` | 71 |
| subtree-link-cut-tree-basic | R-dep(a) | `atl::splay_engine::splay_pushed` | 144 |
| top-tree-basic | R-dep(a) | `atl::top_clusters::sum_tag::add_of` | 77 |
| weighted-dsu-matrix | R-nondep-other | `atl::barrett64::mod` | 43 |
| weighted-dsu-matrix | R-dep(a) | `atl::montgomery::neg` | 102 |
| weighted-dsu-potentials | R-nondep-other | `atl::barrett64::mod` | 43 |
| weighted-dsu-potentials | R-dep(a) | `atl::montgomery::neg` | 102 |
| xor-basis-queries | R-nondep-other | `atl::stamped_xor_basis::stamp` | 111 |

### Known instantiation matches at dependent sites

845 ordinary retained units (268,158 bytes) have a dependent pattern site that resolves to that unit in at least one worker instantiation. These belong to R-AST in the exclusive table. This is the overlapping R-dep(b) observation, not extra bytes.
Overlapping (b) top names: bytes (units): `build` 17,834 (20); `descend` 13,386 (6); `mul` 10,331 (33); `forward_with` 9,999 (11); `inverse_with` 9,746 (11); `find_or_prepare_impl` 6,475 (7); `scale_down` 6,105 (11); `convolve` 5,962 (11); `reduce` 5,797 (17); `push` 5,470 (12); `square` 5,159 (11); `add` 4,830 (15).

### Fallback and validation results

* fast-io-roundtrip: unknown conditional in user code at atl/include/atl/io/reader.hpp:88 (#if)

Standalone x86_64 GCC sample checks completed: 85/85. QEMU output is compared byte for byte with ATL `.out`, using `.in` where present.


## Rule-1 kinds

Follow-up based on `origin/main` (`b6b6226`), ATL `5e2a6e2`, CSES, C++23,
`-O2 --strip-comments --no-line-markers`. The prior `calls-analysis` branch was
fetched locally; its uncommitted review, diagnostic patch and all 85 models were recovered.

Completed: **85/85 examples**; 838 old-style units /
**237,583 bytes** reconciled. **73 explicit-(3) declarations / 18,234 bytes**.

### Declaration totals

Each cell is **written declarations / owned comment-stripped bytes**. Overloads
and written redeclarations are separate; implicit compiler-generated declarations
and extra template instances do not add counts. Shared headers count once per
example occurrence. Default and copy/move constructors take precedence over
explicitness; default-callable constructors with default arguments are default
constructors. The converting row requires at most one required argument and
at least one accepted argument, and excludes those special constructors.

| Kind | (1) Resolved AST | (2) Implicit / trait only | (3) No reference | Total |
| --- | ---: | ---: | ---: | ---: |
| ctor-explicit | 95 / 24,971 | 2 / 1,564 | 73 / 18,234 | 170 / 44,769 |
| ctor-implicit-converting | 25 / 3,694 | 0 / 0 | 17 / 2,141 | 42 / 5,835 |
| ctor-other | 81 / 30,114 | 5 / 1,983 | 121 / 44,834 | 207 / 76,931 |
| default ctor | 129 / 13,592 | 18 / 637 | 124 / 8,006 | 271 / 22,235 |
| copy/move ctor | 1 / 31 | 0 / 0 | 11 / 916 | 12 / 947 |
| destructor | 4 / 219 | 2 / 129 | 0 / 0 | 6 / 348 |
| copy/move assignment | 0 / 0 | 0 / 0 | 10 / 1,764 | 10 / 1,764 |
| operator() | 74 / 15,876 | 1 / 109 | 84 / 25,290 | 159 / 41,275 |
| operator-comparison | 19 / 1,626 | 6 / 516 | 64 / 4,988 | 89 / 7,130 |
| operator-arithmetic | 70 / 8,193 | 24 / 2,479 | 154 / 14,583 | 248 / 25,255 |
| operator-stream | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| operator-other | 29 / 2,802 | 0 / 0 | 67 / 8,292 | 96 / 11,094 |
| conversion-explicit | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| conversion-implicit | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| deduction guide | 0 / 0 | 51 / 11,046 | 56 / 15,282 | 107 / 26,328 |
| virtual member | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| **Member subtotal (old rule-1 scope)** | 527 / 101,118 | 58 / 7,417 | 725 / 129,048 | 1,310 / 237,583 |
| **Guides (additional, record-owned)** | 0 / 0 | 51 / 11,046 | 56 / 15,282 | 107 / 26,328 |
| **Total including guides** | 527 / 101,118 | 109 / 18,463 | 781 / 144,330 | 1,417 / 263,911 |

The guide row is additional: production does not carve deduction guides into
D3e member units. They are separately owned declarations joined to their class
family. Adding them to 237,583 would silently change the earlier denominator.
Nonmember friend operators are likewise outside that member-unit denominator.
Arithmetic includes shifts and compound arithmetic/bitwise assignments; a shift
operator with a stream parameter would instead be stream. Virtualness is also
recorded independently, so a virtual destructor would stay in the destructor row.
The zero stream/conversion/virtual rows describe this kept worker declaration set,
not the entire ATL API: free stream friends are outside the member denominator,
and software-128 conversion operators are outside the native-128 kept set.

### Evidence and limits

(1) records resolved constructor expressions, operator/member/direct calls,
declaration references, implicit conversions represented by those expressions,
temporary/variable/delete destruction, and user targets inside instantiated system
entities reached from kept user families. Both the traversed family and physical
source owner must survive. This includes other rule-1-retained bodies; it is not
a runtime call-graph or a cascading-removal counterfactual.

(2) has no (1) witness: it records unevaluated operands (`sizeof`, `decltype`,
`noexcept`, `requires`), replayed concrete constructibility/convertibility/assignment
trait overload selection, structural special-member traits, implicit CTAD candidate
participation, or residual Clang referenced/used flags. Trait replay runs after
production dependency extraction, under an unevaluated SFINAE trap; it never adds
retention edges. A guide considered by CTAD is implicit evidence even if another
guide wins. Compiler flags cover probes/synthesized uses whose expression is not
retained in Clang’s AST; their original source cannot always be recovered.

(3) has neither a surviving resolved/implicit witness nor a compiler reference/use
flag in any observed instance. Thus explicit-(3) is a conservative worker candidate
set, not a proof of cross-target removability. Residual flags may include a probe
from discarded text; retaining such rows in (2) deliberately shrinks the candidate
set. Unobserved target instantiations and failed substitutions leaving no AST/flag
remain a portability limitation.

Residual flag-only evidence accounts for **56 declarations / 7,249 bytes**
within (2), not an extra category. The local JSON preserves their signatures and flags.
Both explicit-(2) occurrences are flag-only: `top_tree_impl(std::size_t)` (746 bytes)
and `top_tree_impl(R &&)` (818 bytes), in `top-tree-basic`; no resolved retained
expression identifies their use. The conservative (2) placement avoids treating
those compiler-observed probes as unreferenced explicit constructors.

### Top records

| Record | Declarations | Bytes | (1) bytes | (2) bytes | (3) bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `atl::hash` | 80 | 27,040 | 5,241 | 0 | 21,799 |
| `atl::mod_int` | 247 | 22,762 | 10,974 | 2,054 | 9,734 |
| `atl::static_vector` | 85 | 11,968 | 145 | 540 | 11,283 |
| `atl::detail::link_cut_tree_impl` | 32 | 11,758 | 407 | 424 | 10,927 |
| `atl::detail::top_tree_impl` | 13 | 9,136 | 0 | 1,564 | 7,572 |
| `atl::scalar_ntt` | 11 | 7,370 | 7,370 | 0 | 0 |
| `atl::graph::csr` | 21 | 7,119 | 2,082 | 0 | 5,037 |
| `atl::graph::lca` | 16 | 6,512 | 5,536 | 0 | 976 |
| `atl::graph::detail::edge_scan` | 24 | 6,088 | 4,253 | 0 | 1,835 |
| `atl::flat_hash_map` | 35 | 4,963 | 608 | 0 | 4,355 |
| `atl::treap_sequence` | 14 | 4,723 | 1,230 | 0 | 3,493 |
| `atl::graph::detail::residual` | 4 | 4,692 | 2,346 | 0 | 2,346 |
| `atl::detail::hash_table` | 63 | 4,487 | 589 | 79 | 3,819 |
| `atl::detail::montgomery64` | 16 | 4,336 | 3,794 | 0 | 542 |
| `atl::detail::splay_sequence_impl` | 13 | 4,195 | 0 | 592 | 3,603 |
| `atl::persistent_array` | 36 | 4,140 | 2,784 | 959 | 397 |

### Explicit-(3) candidates

**73 declaration occurrences / 18,234 bytes**,
covering **31 written overloads in 46 examples**.
Two occurrences / 378 bytes are in the existing fast-I/O flat fallback;
the other 71 / 17,856 bytes occur in examples whose pruning analysis succeeds.
The following table aggregates the same written overload across examples.

| Declaration (ATL include path:line) | Signature | Occurrences | Bytes |
| --- | --- | ---: | ---: |
| `atl/ds/static_vector.hpp:85` | `void (R &&)` | 17 | 5,831 |
| `atl/ds/coordinate_compression.hpp:178` | `void (R &&, Proj, Compare)` | 2 | 1,810 |
| `atl/ds/flat_hash_map.hpp:78` | `void (R &&)` | 7 | 1,421 |
| `atl/ds/persistent_list.hpp:223` | `void (Range &&)` | 2 | 1,288 |
| `atl/nt/detail/online_convolution.hpp:192` | `void (const Range &, std::size_t)` | 1 | 866 |
| `atl/ds/flat_hash_map.hpp:71` | `void (std::size_t)` | 6 | 564 |
| `atl/ds/detail/hash_table.hpp:114` | `void (std::size_t)` | 6 | 552 |
| `atl/nt/montgomery.hpp:260` | `void (uint64)` | 2 | 542 |
| `atl/ds/persistent_treap_sequence.hpp:543` | `void (R &&)` | 1 | 490 |
| `atl/graph/lca.hpp:351` | `void (const Graph &)` | 4 | 416 |
| `atl/ds/binary_indexed_tree.hpp:226` | `void (std::size_t, T, Op)` | 2 | 404 |
| `atl/ds/treap_sequence.hpp:445` | `void (R &&)` | 1 | 342 |
| `atl/ds/splay_sequence.hpp:609` | `void (R &&)` | 1 | 315 |
| `atl/ds/persistent_heap.hpp:317` | `void (R &&, Compare)` | 1 | 304 |
| `atl/ds/weighted_dsu.hpp:156` | `void (std::size_t)` | 1 | 263 |
| `atl/ds/persistent_vector.hpp:1282` | `void (Range &&)` | 1 | 257 |
| `atl/ds/link_cut_tree.hpp:852` | `void (std::size_t)` | 2 | 252 |
| `atl/ds/flat_hash_set.hpp:69` | `void (R &&)` | 1 | 243 |
| `atl/ds/binary_indexed_tree.hpp:217` | `void (std::array<std::size_t, Dim>, T, Op)` | 1 | 231 |
| `atl/ds/top_tree.hpp:1662` | `void (commutative_fold_t, Args &&...)` | 1 | 225 |

### One-argument implicit constructors and the proposed documentation convention

**42 one-argument-callable, non-explicit, non-special declaration occurrences /
5,835 bytes** cover seven written overloads. Using a narrow reading of a documented
value conversion, **22 occurrences / 3,379 bytes** do not state such a conversion
in their immediately preceding documentation comment. Of those, **17 / 2,141
bytes** are evidence (3); the other five / 1,238 bytes have resolved AST uses.

| ATL include path:line | Occurrences / bytes | (3) occurrences / bytes | Documentation assessment |
| --- | ---: | ---: | --- |
| `atl/nt/mod_int.hpp:56` | 19 / 2,071 | 0 / 0 | Documented reduction of an integral value into a residue: value conversion. |
| `atl/ds/static_vector.hpp:74` | 17 / 1,853 | 16 / 1,744 | Says it holds the initializer-list elements in order; provisionally construction, no conversion contract. |
| `atl/nt/matrix.hpp:285` | 2 / 794 | 1 / 397 | Says matrix from nested rows; provisionally construction, no conversion contract. |
| `atl/nt/dynamic_mod_int.hpp:100` | 1 / 385 | 0 / 0 | Documented integral-to-residue reduction: value conversion. |
| `atl/ds/splay_map.hpp:114` | 1 / 296 | 0 / 0 | No adjacent documentation comment for the policy/pool constructor. |
| `atl/ds/top_clusters.hpp:354` | 1 / 75 | 0 / 0 | Doc says an edge of the given length; the separate inline comment says the length converts. |
| `atl/ds/treap_map.hpp:106` | 1 / 361 | 0 / 0 | No adjacent documentation comment for the policy/pool constructor. |

With unchanged worker references, making all 22 explicit would add **17
declarations / 2,141 bytes** to explicit-(3), giving an estimated **90 declarations /
20,375 bytes**. This is static reclassification, not a tested source change:
explicitness can change copy-list initialization, conversions, traits and overload
resolution, so the modified ATL would need a fresh sweep.

The estimate depends on the intended meaning of “value conversion.” If the
documented list-to-container mappings already qualify for the exception, the
two list overloads account for 19 occurrences / 2,647 bytes and **all 17 new
candidates**. Under that reading, the remaining three no-doc-contract occurrences
are 732 bytes, all evidence (1), and explicit-(3) stays **73 / 18,234 bytes**.
`diameter_value(W)` also already has the non-Doxygen inline explanation
`// implicit: an edge length converts`; moving that promise into its doc comment
could satisfy the convention without making it explicit. No ATL source or
documentation was changed in this analysis.

### Cross-target risks

A concrete ATL constructor-overload counterexample exists in
`atl/ds/binary_indexed_tree.hpp:217,236–249`:

```cpp
std::array<unsigned long, 1> dimensions_or_values{3};
atl::binary_indexed_tree<int, 1> tree(dimensions_or_values);
```

The dimensions constructor takes `std::array<std::size_t, Dim>`. The nested-range
constructor explicitly excludes that same type. On x86_64 LP64, `size_t` is
`unsigned long`, so the dimensions constructor wins and the range constructor is
unused. On i386 ILP32, `size_t` is `unsigned int`; the identical argument is now
accepted by the range constructor, and cannot initialize the dimensions array.
The AST dumps show the different constructor signatures. The pinned worker
classifies the 676-byte range declaration as explicit-(3) in this diagnostic
program; the dimensions constructor has a resolved AST call. Removing only the
nested-range constructor from a worktree-local copy of this ATL header preserves
the x86_64 syntax check and makes the i386 check fail. The original ATL clone
was read-only. This is an added diagnostic program using real ATL declarations,
not a claim that an existing example changes targets in this exact way.

All four full/deleted ATL checks pass their expected outcomes with Clang 22.1.8.
They use the recovered GCC 15 x86_64 headers for syntax checks, plus an empty local
`gnu/stubs-32.h` shim because this is not a 32-bit libc installation; no linking
or i386 runtime result is claimed. The constructor selection is visible directly
in the two AST dumps. No target macro branch was added to the user source.

Trait-dependent library paths remain a separate risk even when no constructor
call survives on the worker. For example, ATL's
`detail::persistent_ring_backing` in `persistent_adapters.hpp:30–38` requires
`std::constructible_from<Backing, std::size_t>`, and the persistent-DSU backing
concept in `persistent_dsu.hpp:24–30` tests construction with `size_t` and an
integer value. Different typedefs can test a different overload; removing an
unreferenced overload can change constraint satisfaction, select another path,
or reject the program. `top_tree.hpp:1663–1669` also uses constructibility of
its own implementation to constrain delegating tag constructors. Explicitness
does not prevent direct-initialization traits from observing a constructor.
The replayed worker trait witnesses belong in (2), rather than the candidate set.
A header-free two-target check additionally makes the trait argument depend on
`sizeof(long)`; deleting the worker-unused constructor flips the other target's
compile-time path. Its four full/deleted checks pass the expected outcomes.

Removal can also expose an ambiguity on another target. In the checked minimal
case, explicit `C(int)`, `C(long)` and `C(double)` overloads receive a typedef
that is `long` on LP64 and `int` on ILP32. Deleting worker-unused `C(int)` leaves
the worker unchanged, but the ILP32 call becomes ambiguous between the remaining
two conversions. All four target/deletion checks pass. No existing ATL sample
was found to exhibit that exact three-scalar-overload ambiguity; the real ATL
array/size-type case above already establishes target-sensitive constructor
selection. Changes to constraints, traits, implicit deduction guides generated
from constructors, and effect analysis also need proof before a removal rule is
sound. **Explicit-(3) bytes are an analysis opportunity, not approved savings.**

### Follow-up build, replay and verification

The follow-up finished within the requested 50-minute budget (started 23:11 UTC
on 2026-11-20). Downloaded the project release's pinned LLVM **23.1.5 Linux ARM64**
SDK again, checked its published SHA-256
`5ca712f18c710592a8fa89758246a6b6cbdbd16c1fbd5f12c7f5bcdf35fe06de`, and built
under ignored `spike-work/` and `build/` with Clang 22.1.8, `-O1 -DNDEBUG`, LLD 22
and **`-j2`**. Acquisition/configure/build had 600/120/900-second hard timeouts;
diagnostic rebuilds had 600-second caps. The SDK was deleted after verification.

All **85** saved CSES worker sources were freshly parsed by the pinned worker
using their original parse arguments and the recovered GCC 15.2 x86_64 headers.
The replay requires exact equality with the previous model for every declaration
identity, label, member owner, owned byte range, AST edge, dependent-name set,
member blocker, union, system entity and system-use edge. Only after those checks
pass does it carry over the original full-pipeline retention decisions. This
preserves the actual profile-conditioned kept set, including the fast-I/O refusal;
it does not derive retention from a replay with a missing judge-macro-fact snapshot.
The physical source map reconstructed from the saved `#line` directives is also
verified against fresh pipeline runs. The 838 units / 237,583 bytes reconcile
exactly, and all per-declaration byte/count totals pass consistency checks.

**28 fresh full-pipeline example runs** also matched the previous artifact bytes,
source maps, declaration metadata, original compiler reference/use flags and AST
graphs exactly. After correcting declaration identity, a further full convolution
run matched **every diagnostic reference** from its replay, as well as the prior
artifact. The diagnostic binds evidence to function declarations: references to
constructor parameters or locals do not count as references to their owning
constructor. A focused overloaded-constructor fixture verifies a construction
inside `std::make_unique`, constructibility/nothrow traits, an unevaluated operand,
and an unused constructor that itself reads its parameter. Its evidence classes
are (1), (2), (2), (2), (3), respectively; baseline/diagnostic artifact bytes match.
CTAD participation is checked in the fixture too.

All six focused CTest checks pass again after the final diagnostic change:
`unit`, `embedded_members`, `prune_extract_cases`, `prune_extract_models`,
`member_pruning`, `member_review`. CTest has 120-second per-test limits and a
240-second outer cap. The twelve target/deletion checks described above pass
all expected outcomes. Analysis tooling compiles as Python; the reconciliation,
classification, candidate and documentation-count audits pass. This follow-up
reuses the previous 85 sample-output validations for identical artifacts; it does
not claim to have rerun all 85 samples.

Production sources were restored. This review document is the only intended Git
change; nothing was committed or pushed. The complete follow-up diagnostic patch
is retained locally at `measurements/rule1/diagnostic.patch`, SHA-256
`dcc2e42c0415f537737b5f2e1071b21f70342299796ca2cf19adecc2f9da0468`.
The ignored `measurements/rule1/` directory also contains replay/classification,
documentation-audit and target-check scripts plus final summaries; exact raw
models, protocol inputs, logs and target AST dumps are under `.checks/rule1/`.
The inherited diagnostic patch and all prior evidence remain under
`measurements/member-call/` and `.checks/member-call/`. The SDK is absent and
`git diff --check` passes.
