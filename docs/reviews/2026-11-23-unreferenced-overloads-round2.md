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
