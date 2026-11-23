Round 2 of the `unreferenced-overloads` prototype, starting at e295016 and
comparing against a separately built archive of a653b6c. No commit was made.
The supplied `.measure/` directory was absent from this worktree, so the
original investigation and counts below use repository fixtures. The subsequent
ATL sweep supplied for adoption is recorded next.

## Adopted round 2: ATL sweep

The user approved round 2 as default-on A13 `unreferenced-overloads` on
2026-11-23, with `--no-assume=unreferenced-overloads` as the opt-out. Sweep
45187236147 compared 126 ATL examples under judge `cses` against main a653b6c:

| Rendering | Main bytes | Round 2 bytes | Change |
| --- | ---: | ---: | ---: |
| NLM | 4,535,428 | 4,451,696 | −83,732 (−1.85%) |
| Compact | 4,051,059 | 3,972,342 | −78,717 (−1.94%) |

All 126/126 examples pruned, with 0 run failures. No example grew; 41 shrank.
The removal summaries count 253 declarations / 65,500 B within surviving records:

| Category | Declarations | Bytes |
| --- | ---: | ---: |
| operator() | 114 | 35,885 |
| Deduction guides | 100 | 23,987 |
| Arithmetic/comparison/other operators | 31 | 4,295 |
| Constructors | 8 | 1,333 |

## Round 3 (not adopted)

Flag-origin replay plus check-shape narrowing was evaluated and discarded.
It added 1,902 B on 7 examples for roughly 790 lines of implementation and up
to 8 reparses. All 43 flag-protected constructors examined there had real
references from retained code. Round 2's compiler used/referenced protection
remains; none of the round 3 logic is adopted.

## Review fixes

The worker-side review was reproduced before changing extraction, against
35d1ce4. These are worker invariants; A13 only constrains the judge and cannot
justify changing a locally checked result. The `rule1_review_*` fixtures use
the existing original/pruned GCC and Clang runtime oracle, now with optional
`dialect.txt` and `exit.txt` metadata (and compiler/original/pruned diagnostics).
All original programs print `0` and exit 0.

| Review finding | Unfixed pruned result | Fix |
| --- | --- | --- |
| 1, reversed equality suppression | GCC 15 and Clang 22 print `1`, exit 1 | Keep `==`, `!=` and `<=>` structurally. |
| 2, implicit equality generation | Both print `1`, exit 1 | Same comparison protection, independent of viability or references. |
| 3, assignability and destination constructors | Both print `1`, exit 1 | Assignment traits witness constructors as well as other overloads. |
| 4, constructor parameter records | Both print `1`, exit 1 | Recursively witness constructor/assignment parameter records, stripping cv/ref qualifiers. |
| 5, C++17 aggregate classification | Both print `1`, exit 1 | Preserve one user-provided, explicit or inherited constructor before C++20. |
| 6, constexpr parameter's literal type | Clang rejects the pruned definition; GCC 15 accepts it | Preserve one constexpr non-copy/move constructor in every dialect. |
| 7, selected guide template | Already prints `0`, exit 0 with both compilers | Register written guides in the same identity map as methods, so specialization used/referenced flags explicitly reach the written template. Retain the review example as a regression, with an unused guide-template control. |

All seven comparison operators are now protected, including `<`, `>`, `<=`
and `>=`. This is conservative: defaulted comparison synthesis can depend on
unsuccessful lookup, which selected-call AST references do not model. Removal
of a whole unused record remains possible. Functor `operator()` and unused
deduction guides remain individually removable.

The constructor witness walk also follows assignment operator parameter types
and bases, and works from a forward declaration's definition. Instantiated
constructor and assignment template signatures are inspected as well as their
written patterns:
a defaulted template argument can supply a concrete record absent from the
pattern's dependent parameter type. Both additional template-parameter fixtures
reproduced `0` to `1` changes under GCC and Clang before this extension. A third
fixture checks the same negative result without any constructor call. Each
operand walk is bounded to 128 distinct records and 4096 declarations (including
specialization signatures), parameters, bases and template arguments. Record
specialization arguments are witnesses too: `initializer_list<Arg>` constructs
`Arg` elements even though its own constructor signatures only expose pointers.
A list-construction concept fixture also reproduced `0` to `1` changes under
both compilers before this extension. If a
bound is reached, the check witnesses every rule-1 unit, including guides and
operators. Evidence remains separate from ordinary/effect edges and is gated
on independently retained owners. The bound fixture checks both the fallback
and removal of a dead owner.

The expanded fixtures cover cv/ref assignment parameters, standard construction
and assignment traits, requires/concepts, unevaluated SFINAE checks, forward
records, all comparison spellings, the traversal bound, C++17/C++20 literal
constructors, and the conservative C++23 literal guard. A C++23 property fixture
also verifies that `=default` and `=delete` count as user-declared constructors:
the structural declaration survives while the other constructor is removed.
One qualifying constructor is enough for each type-property guard; an already
protected qualifying declaration is preferred.

The standard assignment fixture exposed another evidence gap:
`std::is_assignable_v` can have its initializer folded to a boolean before the
dependency walk. System variable-template specializations now propagate the
written pattern's check classification to their actual arguments even when
there is no current user family.

The existing expectation changes are limited to two files:

- `rule1_overloads/expect.txt`: require the unused `Overloads::operator==` to
  remain and change the comparison-removal report count from 1 to 0.
- `rule1_evidence/expect.txt`: require `Compared::operator!=` to remain with
  structural protection rather than be removed.

Existing fixture stdout/exit expectations and opt-out expectations are unchanged.
The 21 new review fixtures add explicit retention/removal checks and runtime
oracles, including an unused guide template and functor removal controls.

For the 75 existing fixtures accepted by the local CLI baseline compiler, the
rule-1 removal counters within surviving records change as follows (counts
exclude the newly added review fixtures):

| Category | Before declarations / bytes | Fixed declarations / bytes |
| --- | ---: | ---: |
| Explicit constructors | 2 / 94 | 2 / 94 |
| Implicit converting constructors | 1 / 32 | 1 / 32 |
| Multi-argument constructors | 11 / 291 | 11 / 291 |
| operator() | 2 / 86 | 2 / 86 |
| Comparison operators | 2 / 126 | 0 / 0 |
| Arithmetic operators | 11 / 447 | 11 / 447 |
| Other operators | 1 / 40 | 1 / 40 |
| Deduction guides | 1 / 23 | 1 / 23 |
| Total | 31 / 1,139 | 29 / 1,013 |

NLM rendered savings against opt-out change from 1,326 B to 1,194 B: retaining
the two declarations adds 126 declaration bytes and 6 indentation/newline bytes.
The only
existing fixtures with different removed bytes are `rule1_overloads` (572 to
515 B) and `rule1_evidence` (69 to 0 B); their respective comparison declarations
now survive. All 14 constructor removals and all functor/guide removals remain.
The ATL corpus is absent here, so the adopted 35,885 B functor and 23,987 B guide
sweep totals were not remeasured; no blanket functor/guide protection was added.

The SDK worker is LLVM 23.1.5; runtime oracles use GCC 15.2.0 and Clang
22.1.8. The assertion-enabled build uses `-O0 -g0`, at most two build jobs,
`ulimit -v 5700000` and command timeouts. All SDK downloads, compiler temporary
files and logs remain inside this worktree.

All 409 Python tooling tests passed: 209 unittest cases across extensions,
profiles, drift, LLVM CI, release, retry, measurement and matrix smoke tests,
and 200 library pytest cases. Library modules ran in separate processes;
the large reviewed-baseline round trip ran with `PYTHONMALLOC=malloc` and
`MALLOC_ARENA_MAX=2` under `ulimit -v 2500000` after combined runs were killed
under memory pressure.

All 109 CTest tests passed: the final 107-test batch includes all 122 extraction
expectation variants (21 new review fixtures), model checks, member review,
CLI options, pipeline suites and 81 caide cases. Every enabled extraction
fixture also checks family/provenance and physical declaration coverage against
opt-out; runtime fixtures compare expected stdout/exit for original and pruned
programs under both GCC and Clang. The unit suite and `pipeline_cli_d3c` passed
in isolation after loaded runs hit their existing 120-second deadlines: 97.23 s
for the unchanged unit executable and 109.69 s for the final D3c CLI rerun.
No checked-in timeout was increased.

The final NLM CLI differential passed on 96 accepted fixtures (75 existing and
21 new), with five matching baseline-gate rejections: `chrono_parse`,
`generated_line`, `ordinary_pragmas`, `parse_error` and `typical_library`.
Opt-out rendering is byte-identical to archived 35d1ce4 with A13 disabled;
enabled families/declaration bytes are monotone and NLM output never grows
against opt-out. The seven existing focused fixtures also pass CSES comparisons
in plain, NLM and compact rendering; plain marker growth is reported separately.
Only the two existing outputs listed above differ from 35d1ce4 with A13 enabled.

Reproduction and final logs are under ignored `build/`: `review-before.log`,
`template-parameters-before.log`, `assignment-template-before.log`,
`negative-template-before.log`, `list-parameter-before.log`, `ctest-final.log`,
`ctest-isolated-final.log`, `ctest-d3c-final.log`, `cli-final/summary.json`,
`focused-results.json`, `python-library-results.json` and `review-counts.json`.
The before CLI/worker snapshot is in `build/before/`; the final stable snapshot
is in `build/verified-bin/`. The focused oracle is reproducible with
`timeout 240s build/current/tests/runner/distill_prune_cases
build/verified-bin/distill_prune_worker rule1_review` (set `TMPDIR` inside this
worktree and apply the memory limit above). No commit was made.

## Review fixes, round 2

The follow-up A–C programs were added as `rule1_round2_*` extraction fixtures
before changing the worker, then run against the archived 2f9d095 worker.
The existing oracle compiled and ran original and pruned programs with GCC
15.2.0 and Clang 22.1.8, checking both stdout and exit status:

| Finding | Original stdout / exit | Unfixed pruned stdout / exit |
| --- | --- | --- |
| A, aggregate member conversion | `0` / 0 | `1` / 1, both compilers |
| B, deleted implicit move assignment, evaluated call | `1` / 1 | `2` / 2, both compilers |
| C, deleted defaulted move assignment, explicit trait | `0` / 0 | `1` / 1, both compilers |

The constructor/check witness walk now follows non-static data members,
including cv/ref types, multidimensional array elements and anonymous
union/struct fields. Bases and members retain assignment-operator evidence
for assignment checks. The visited-record cache tracks evidence kinds, so a
record first reached for constructors can later acquire assignment evidence.
A separate fixture exercises that upgrade without any deleted special member.

Deletion dependencies are independent of check witnesses. A deleted implicit
or defaulted default/copy/move constructor, copy/move assignment or destructor
protects every constructor and every `operator=` overload (including templates)
of its direct/virtual bases and member record types, transitively through their
bases/members. User-written `=delete` does not itself trigger this protection.
Non-deleted special members need no dependency protection: removing a
non-selected candidate cannot undelete a member that is already non-deleted.
Protection remains conditional on the candidate's owner surviving.

Clang declares implicit members lazily. The consumer now obtains the parser's
Sema via `SemaConsumer` and calls `ForceDeclarationOfImplicitMembers` for every
complete non-dependent user record, including implicit lambda records and
known template instantiations. Dependent patterns cannot be forced; undeclared
implicit members there conservatively protect subobject constructor/assignment
candidates, with no protection when no such candidates exist. Both walks are
bounded to 128 records and 4096 declarations; deletion-walk exhaustion protects
all rule-1 declarations in the TU. The bound fixture also verifies that a dead
owner remains removable.

The existing `rule1_review_guide_template` fixture already contains the exact
`Box(T) -> Box<long>` regression and an unused `Box(T*) -> Box<char>` control.
In `MemberUnits::VisitFunctionDecl`, the evidence-only identity lookup and
`protectRule1` call precede the `CXXMethodDecl` filter, so non-method guide
specializations transfer used/referenced flags through `rule1Identity` to their
registered written pattern. A code comment records this ordering requirement.

Additional fixtures cover aggregate fields with a non-deleted default
constructor, field shapes, ordinary assignment through member and virtual-base
subobjects, lazily declared unused move assignment, each special-member kind,
implicit lambda records, dependent patterns, explicit-delete/non-deleted removal
controls, and the bounded fallback. The 15 new fixtures use the same GCC/Clang
stdout/exit and opt-out monotonicity oracles as the existing fixtures. Baseline
reproduction is in `build/round2-before.log`.

All 409 Python tests passed again: 209 unittest cases and 200 library pytest
cases. Library modules ran in separate processes under the memory limit; the
large reviewed-baseline test passed with capture disabled (`pytest -s`).

All 109 CTest tests passed across the full batch and the final isolated D3c CLI
rerun. The full extraction suite passed all 137 expectation variants, including
15 new fixtures; the focused A13 run passed 53 variants. The original batch
passed 108 tests, with `pipeline_cli_d3c` reaching its 120-second aggregate
limit. Timing the unchanged archived CLI's 14 D3c cases took 117.76 seconds;
the fixed CLI passed the same assertions directly in 121.24 seconds. This
suite's aggregate CTest timeout is now 180 seconds; its per-case process,
compilation and runtime limits are unchanged. The CTest rerun passed in
126.43 seconds. No other checked-in timeout changed.

Builds used the pinned LLVM 23.1.5 SDK, assertions, `-O0 -g0`, at most two jobs,
`ulimit -v 5700000` and explicit timeouts. All compiler temporary files, SDK
copies and validation artifacts stayed inside this worktree. Final CTest logs
are `build/ctest-final.log` and `build/ctest-d3c-180.log`; Python results are
`build/python-*.log`. The stable tested CLI/worker snapshot is in
`build/verified-bin/`. No commit was made.

The final NLM CLI differential passed on 111 accepted fixtures (96 pre-existing
and 15 new), with five matching baseline-gate rejections: `chrono_parse`,
`generated_line`, `ordinary_pragmas`, `parse_error` and `typical_library`.
Opt-out output is byte-identical to archived 2f9d095 with A13 disabled; enabled
families, physical declaration coverage and declaration bytes are monotone,
and NLM output never grows against opt-out.

Rule-1 removal counts within surviving records on the **96 pre-existing
accepted fixtures** are unchanged:

| Category | Before declarations / bytes | Fixed declarations / bytes |
| --- | ---: | ---: |
| Explicit constructors | 2 / 94 | 2 / 94 |
| Implicit converting constructors | 3 / 64 | 3 / 64 |
| Multi-argument constructors | 12 / 321 | 12 / 321 |
| operator() | 3 / 122 | 3 / 122 |
| Comparison operators | 0 / 0 | 0 / 0 |
| Arithmetic operators | 11 / 447 | 11 / 447 |
| Other operators | 1 / 40 | 1 / 40 |
| Deduction guides | 2 / 62 | 2 / 62 |
| Total | 34 / 1,150 | 34 / 1,150 |

All 17 constructor removals, three functor removals and two guide removals
remain. NLM rendered savings against opt-out remain 1,344 B. This comparison
includes the previous review's 21 fixtures and excludes the 15 new round-2
fixtures. Per-category totals and per-fixture differences are recorded in
`build/round2-counts.json`; the CLI differential is in
`build/cli-final-round2/summary.json`. Completed serial comparison artifacts
were reused with identical binaries, inputs and options; all comparison
invariants were rechecked, and remaining CLI invocations ran at most two at a
time under the same memory/time limits.

None of the 96 pre-existing accepted NLM outputs changed from the archived
A13-enabled branch. The seven focused CSES fixtures also passed opt-out byte
identity and family/declaration monotonicity checks in plain, NLM and compact
rendering. NLM and compact output never grew. Plain rendering reported extra
`#line` marker bytes for `rule1_layout_checks` (78 B),
`rule1_nested_protection` (186 B) and `rule1_specialization_protection` (196 B),
with monotone family/declaration coverage. Their summaries are
`build/focused-round2-{plain,nlm,compact}/summary.json`.

## Original round 2 investigation

The reproduced causes and changes are:

| Cause | Reproduction and fix |
| --- | --- |
| Relaxing a rule-1 blocker also removed the old owner-dispatch boundary in the reverse effect graph. A constructor with a library-spelled record name could become an unbounded hook and disable all pruning. | `rule1_effect_dispatch`: the prototype refuses through `get::get` and retains unrelated `discarded`. Rule-1 units now retain the old dispatch treatment for effects, independently of forward removal eligibility. |
| Enabling implicit traversal during member discovery created separate lambda call-operator units, changed byte ownership, and added aggregation/blocker paths. | `rule1_lambda_monotonic` checks a live lambda that names a helper and a dead member whose lambda uses a functor. Discovery now uses the same non-implicit traversal as main; a separate implicit pass only gathers evidence. No separate lambda units are emitted. |
| Check witnesses were ordinary AST edges, including reverse effect edges. Extra trait/concept operand traversals could consequently resurrect owners and introduce new potential triggers. | User and system check evidence now have separate serialized vectors. Reached evidence waits for an independently retained owner and never enters the reverse effect graph. Unit tests check both gates, system checks, and the absence of reverse triggers. Extra operand inspection collects evidence without adding ordinary dependencies. |
| Every `sizeof`/`alignof` and type-classification trait marked enclosing templates as checked, retaining every constructor/operator on later type arguments. | `rule1_layout_checks`: the prototype retains two unused constructors, their helper, and an arithmetic operator. Layout, identity, classification, and checks of already protected special members no longer create blanket overload evidence. Construction/conversion traits and assignment/comparison traits use separate evidence kinds. |
| libstdc++ implements some special-member traits using dependent `decltype` calls, not just builtins. Treating these as arbitrary checks still protected unrelated constructors. | The layout fixture includes `is_destructible`. Expression evidence distinguishes protected destructor checks and scalar-argument helper calls from construction, dependent-return deduction, unknown calls, and arguments that can require class construction. |
| A check inside a method or an unused nested class template contaminated its enclosing class templates. | `rule1_layout_checks` and `rule1_nested_template_check`. Witness propagation stops at the relevant callable/template. Lambda/local-class checks also propagate to the enclosing callable so captured template arguments remain witnesses. |
| Nested records share structural families. Indexing witnesses and inherited bases by family protected unrelated nested/outer constructors. Qualifier traversal also treated `Outer` in `Outer::Nested` as a checked type. | `rule1_nested_protection`: witnesses and inherited bases use written record keys. Direct trait evidence inspects complete operand types rather than every qualifier visited by RAV. |

`potentialEdges` are still needed for unresolved candidates in the reverse
effect graph; they are not forward keep edges. The ordinary name indexes are
multimaps, so removing rule-1 entries cannot redirect a match to a different
family. Rule-4/5 spelling protection was already bypassed for rule-1 units in
the prototype; it was not a universal constructor blocker. Constructor
overloads already had distinct declaration identities, with redeclarations
and specializations sharing the appropriate identity. Those rules remain,
and the existing overload/redeclaration tests still exercise them.

There was no universal "keep all constructors" condition: the unmodified
prototype passed its 95 extraction cases and removed constructors in its
small overload fixtures. The newly reproduced zero-removal paths were broad
check witnesses, enclosing-template contamination, and record/family
confusion. Compiler `isUsed`/`isReferenced` evidence remains intentionally
protected even when the reference occurs in a discarded function, as required
by the brief. Structural declarations and CTAD evidence remain protected.
The last-constructor loop still runs after closure, retains one written
identity per surviving record, and cannot resurrect a dead owner.

Reports now include `rule1-constructors-kept: reason=... declarations=N`.
Counts deduplicate instantiated copies by written declaration offset and
include written redeclarations. A declaration is attributed to its explicit
blocker when present; otherwise aggregation is followed to its primary keep
reason. Counts describe effective protection, not every overlapping possible
reason. Existing `rule1-removed` category/byte counters remain available.

For the seven focused CSES fixtures (`rule1_overloads`, `rule1_effect_dispatch`,
`rule1_lambda_monotonic`, `rule1_layout_checks`, `rule1_nested_protection`,
`rule1_nested_template_check`, and `rule1_sizeof_construction`), 23 written
constructor declarations are retained with these effective protections:

| Protection | Declarations |
| --- | ---: |
| Structural declaration | 13 |
| Compiler used/referenced evidence | 3 |
| Overload-sensitive check | 3 |
| Inherited constructor base | 2 |
| Last user-declared constructor | 1 |
| Instantiation effect | 1 |
| Rule-4/5 spelling protection | 0 |
| CTAD evidence | 0 |

Eight further constructor declarations are removed: two explicit one-argument,
one implicit converting, and five multi-argument. These counts do not include
constructors whose containing record disappears. The safety fixture checks
construction inside `sizeof`, including lambda captures, dependent return-type
deduction, and conversion of a call argument, in a branch discarded locally.

The extraction runner checks enabled retention against opt-out on every
enabled fixture, both by family label/provenance and by physical declaration
coverage. This catches growth even when aggregate output size decreases.
`tests/runner/compare_overloads.py` additionally compares actual main and
revised CLI executables: opt-out bytes must match exactly, enabled kept
families and retained declaration bytes must be a subset, and marker-free
enabled output must not grow. It accepts
`--render plain|nlm|compact`, `--judge`, and repeated `--case` filters.
Matching CLI baseline-gate rejections are counted separately, with all three
executables/modes required to agree. Plain rendering records marker overhead
separately: the focused layout/nested fixtures add 6, 114, and 141 bytes of
`#line` markers respectively despite retaining fewer declaration bytes.

Validation: 101 extraction expectation variants passed, including the
monotonicity checks and original/pruned GCC and Clang runtime oracles. The
`unit`, `pipeline_cli`, `pipeline_pruning`, `cli_options`,
`prune_extract_models`, and `member_review` CTest suites all passed. On the
75 fixtures accepted by the local CLI baseline compiler, opt-out output is
byte-identical to a653b6c, enabled retained families/bytes are monotone, and
enabled `--no-line-markers` output never grows (1,326 bytes removed overall).
The other five fixtures have matching baseline-gate rejections: missing local
chrono support, a deliberate unsupported generated `#line`, the deliberate
parse error, and two x86-pragma fixtures on this AArch64 VM. Seven focused
fixtures also pass CSES/NLM, plain, and compact comparisons. For the discarded
branch safety fixture, replacing `sizeof(long) == 4` with `true` in both the
original and pruned output compiles/runs with GCC and Clang and prints `7` in
all four runs; this checks branch selection, not a different ABI.

Across the 75 accepted local fixtures, constructor protection counts are:
structural 28, compiler used/referenced 10, overload-sensitive checks 5,
inherited base 6, last constructor 3, instantiation effects 2, and AST
reference/aggregation 1. Fourteen constructors are removed (2 explicit,
1 implicit converting, 11 multi-argument). Detailed reports and logs are in
`build/verified-*`; `build/verified-summary.json` combines the 75 successful
comparisons with the five separately checked matching rejections. The optional
full differential driver is reproducible with
`python3 tests/runner/compare_overloads.py --main build/main-build/distill
--new build/verified-bin/distill --output build/accepted-nlm` (set `TMPDIR`
inside the worktree and run under a timeout and memory limit).

All SDK files, baseline sources/binaries, compiler temporary files, and
validation artifacts are under ignored `spike-work/` or `build/` directories
inside this worktree. Builds use at most two jobs, a 5.5 GB virtual-memory
limit, and command timeouts. The build enables assertions with `-O0 -g0`.
The worker protocol is version 9 because evidence is separate from AST edges.

Remaining limits: true overload-sensitive checks still conservatively
protect multiple candidate declarations; there is no complete argument-level
viability or template-parameter-flow analysis. Requires/concept witnesses
remain conservative. Compiler reference flags from dead callers are retained
by policy. Macro/shared spellings and explicitly instantiated records retain
their existing protections. The original local investigation did not include
the ATL corpus/reports; the adoption sweep totals above were supplied
subsequently, and no ATL per-protection-reason breakdown is claimed.
