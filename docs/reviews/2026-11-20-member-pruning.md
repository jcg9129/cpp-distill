<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Adopted design: pruning unused member functions of kept records

Date: 2026-11-20. User decision: policy **D3e**, V1 (library-spelled),
under A12. V2, its provisional registry and the environment variant switch have
been removed. D3d-7's instantiation-effect rules remain in force, including A11
and its independent opt-out. See [design §4.7](../design.md#47-pruning).

## Assumption A12 (`library-spelled-members`)

> The judge's standard library headers call, name or take the address of a member
> of a user type only through an identifier spelled in, or produced by macro
> expansion of, the library headers the translation unit includes on the worker,
> or spelled in the headers of a scanned library build, or as an operator
> function, conversion function, constructor or destructor.

User, 2026-11-20. This replaces the proposed A10 member-name registry assumption;
A10 was never adopted. The library-name index alone was insufficient (range
formatting probes `set_debug_format`). A12 uses actual worker headers plus all scanned library builds and assumes
that this bounds the judge's library; it does not prove matching headers.

After collecting the include graph, the worker raw-lexes every included system
header, including transitive headers, inactive branches and macro definitions.
Comments and string contents do not contribute identifiers. Identifier spelling
is normalized by Clang. The inventory also includes identifier tokens actually
produced by preprocessing the full unpruned translation unit through system
macros, including pasted results, user-supplied arguments and user macros invoked
in system context. Expansion ancestry is followed through spelling and invocation
locations, including scratch buffers. Under A12 these headers and the scanned
inventory bound the judge's; unexpanded worker macros produce nothing in this TU. An unreadable or
incomplete worker inventory disables member pruning with a report reason. Whole-family
pruning and its existing safety gates continue. Language lookup names (`begin`,
`end`, `get`, and coroutine protocol names) are protected independently of headers.
The explicit union point now adds every identifier spelled in the reachable
headers of every eligible scanned L1c libstdc++ build, including libc headers.
Capture raw-tokenizes those headers without conditional filtering, normalizes
line splices/UCNs and excludes comments/literals/pp-numbers. Merge stores one
sorted union and compact per-build counts, name digests and token-pasting flags;
full evidence retains per-build names and pasting header keys. Pasting is
report-only; worker expansions still cover local pasting. This can only keep
more members. The report names the scanned-build count and generator.
Generator 4 produces the inventory; generator 3 remains accepted and reports
`scanned-build inventory unavailable`, using worker headers only until a reviewed
CI import. Dispatch `.github/workflows/library.yml` on the updated branch with
no inputs, then review/import the `library-generated` artifact as described in
[ci/library/README.md](../../ci/library/README.md#first-capture-or-reviewed-refresh).

`--no-assume=library-spelled-members` disables only member pruning. It uses the
same repeatable/comma-list parser as `instantiation-effects`. Every enabled member
analysis reports **A12 library-spelled-members**, including analyses that remove
no member, following A11's convention. Disabled modes omit A12 and explain why.
Both flags are immutable parent/worker identity fields, serialized in private
protocol version **7** after the owner-dispatch representation fix below. There is
no pruning-result cache; serialized model identity includes both flags and the
collected inventory.

## Rule (policy D3e)

A *member unit* groups all functions and function templates with one name in one
record, including overloads, static/non-static functions, declarations, out-of-line
definitions and specializations. Nested records have separate name groups. Only
function members are removable; data, layout, nested types, aliases, enums and
friends remain with the structural record family.

A unit of a kept record is removed unless one of these holds:

1. **Excluded kinds:** constructors, destructors, conversions, operators,
   deduction guides, or a unit containing a virtual member (including semantic
   virtualness in any known instantiation and explicit `override`/`final`).
2. **Virtual name clash:** any user record declares a virtual with this name.
3. **Reserved names:** the name is a reserved identifier.
4. **Library/language spelling:** the name is an identifier token in any included
   system header's raw text or produced through its macro expansions in this TU,
   or a language member-lookup name. A12 bounds library lookup; an unavailable
   inventory disables member pruning, preserving members of retained records.
5. **Name rule:** the name is an identifier token in kept user text outside the
   unit: kept families/units, macro definitions and structural text. This covers
   calls, address-taking, qualified friends, using-declarations, requires and
   decltype, including same-named functions of unrelated records.
6. **AST/dependent names:** kept code references the member, or a kept template
   pattern contains a dependent member expression/scope reference with its name.
   User-only pasted names are covered by AST edges.
7. **Roots and pins:** non-inert local static/thread storage, interpreted
   attributes/assembly, `--keep`, keep annotations, source directives, macro
   invocation boundaries and indivisible mixed declarations retain their units.
8. **Explicitly instantiated records:** explicit instantiation declarations or
   definitions preserve all members of the record and its class template.

Member-to-owner edges preserve the containing record. Owner retention activates
rules 1–4 and 8. Name-only matches wait for the owner to be kept, so a coincidental
`size` token cannot resurrect an unrelated unused record. Roots and actual AST
references can keep the owner. A unit's own name does not keep it by itself. `--keep` on a record protects
its structural family; bind a member name to pin that member unit explicitly.

## Closure, edits, and critical review

Members participate as families in ordinary closure and the reverse potential
instantiation-effect graph. The latter includes lexical/AST/dependent-name edges,
family unions and system edges over **all** text, not just retained text or locally
observed instantiations. Owner-to-member potential-effect edges include ordinary
member declarations, whose signatures can instantiate dependencies when the
record is instantiated. Forward owner-to-member blocking edges model rules 1–4/8.
A dormant member that may instantiate an effect source remains, along with its helpers.
Units retained with their owner by rules 1–4/8 share the owner's effect component
and incoming system-dispatch analysis, as explained below. Free hooks,
uncertified hidden friends and system specializations still use the existing
`unbounded system instantiation effects` refusal. A11 exemptions and
`--no-assume=instantiation-effects` apply independently.
On inventory failure extraction keeps records whole, using the existing
whole-family instantiation-effect rules.

Member edit ranges include template headers, attributes, default arguments and
terminators. Subtracting them preserves access labels and record braces. Shared
function/data declarations are pinned; same-name overloads retain together.
Normalization retains every disjoint structural fragment. The static-name
invariant checks individually removed members of kept records; members removed
with an entire record may share names with retained records.

Review of the prototype found and fixed these production blockers:

- The first productization refused member pruning for any system `##`, including
  inactive/digraph paste tokens. This disabled members with ordinary headers:
  glibc `stdint.h` defines `__INT64_C(c) c ## L`, and `bits/floatn.h` and common
  libstdc++ headers also paste tokens. The bounded inventory now unions raw header
  identifiers with identifiers actually emitted through system macro expansion
  in the full unpruned TU. Unexpanded paste macros no longer disable pruning;
  pasted member names and user arguments remain protected under amended A12.
- The raw-lexer loop discarded the final token of a header without a trailing
  newline. It now processes that token before stopping.
- Raw identifier bytes bypassed Clang's identifier normalization, missing names
  containing line splices/escapes. The inventory now uses preprocessor lookup.
- Headers first entered as user files, then marked by `#pragma GCC system_header`,
  were missed. The inventory includes system-header pragma callbacks too.
- Canonical bindings could be overwritten before joining an in-class method
  declaration to an already discovered out-of-line template definition. The
  binding now records the previous owner first, preserving one same-name unit.
- The first production fallback retained split members individually and caused
  extra unbounded-effect refusals. Inventory refusal now selects whole-record
  extraction before partitioning, preserving existing whole-family behavior.
- Language-only lookup names were treated as unbounded library hooks even with
  no library headers. The refusal now uses the library inventory separately.
- Partial-specialization instances could be visited before their complete written
  method patterns, pinning lazy methods as unterminated declarations. Seeding
  units from the written pattern first restores complete ranges and pruning.
- Rule 8 now also records the selected partial-specialization pattern when a
  class is explicitly instantiated, rather than just the primary template.
- The environment switch and unapproved A10 reports did not represent the adopted
  policy. D3e defaults, an A12 opt-out, refusal reasons, unit counts and protocol
  identity replace the experimental interface.

Reverse potential-name collisions between independently removable units can
conservatively retain an otherwise unused record. Owner-dispatched units instead
use the whole-record name-index boundary described below; the dependent-member
specialization regression now removes the unrelated `Unused::get` record while
preserving every `Box` specialization and its real effect paths.

The applied prototype already fixed structural-fragment loss in normalization,
qualified-friend ownership, outer template-header ranges and member-template ADL
over-retention. Those fixes and regressions remain. Tests cover all eight rules,
overloads, out-of-line/member-template specializations, default arguments, access
labels/braces, nested/dead records, library-only allocator calls and dormant
library protocols, flag independence, and A11-on/off instantiation effects.
Reports emit one member-unit row per unit plus kept/removed counts, alongside
ordinary family provenance and byte totals.

## Owner dispatch regression

The CSES ATL sweep exposed an extra refusal in `modular-arithmetic/residues`:
splitting `dynamic_mod_int`'s constructors made them new unbounded library hooks
in the reverse closure of the class template's static inline reducer. This
was a partitioning artifact: rule 1 never removes a constructor while its owner
survives, and D3d-7 already analyzed its text and dependencies in that owner.
The same issue applies to destructors, conversions, operators, deduction guides,
virtual units and explicitly instantiated records (rule 8). After removing the
constructor refusal, the example also exposed `dynamic_mod_int::value` and
`mod_int::value`: rule 4 always retains these library-spelled units with their
owners. Rules 2 and 3 likewise never separate their units from a kept owner.

The fix preserves separate edit/report rows but treats every unit statically
retained by rules 1–4/8 as part of its owner's effect component. Owner-to-unit
and unit-to-owner effect edges preserve all potential dependencies and triggers;
incoming member dispatch uses the owner's D3d-7 analysis. A blocker on any
declaration applies to the whole aggregated unit, including nonvirtual overloads
of a virtual name. Deduction guides already union with their deduced template.
The owner's free/hidden hooks and system specializations retain their original
checks, including the written-own-record carrier certificate. The change does
not certify an arbitrary free callback or a target-selected alias as bounded.
The reverse name index likewise excludes these structural member names, matching
D3d-7: indexing them globally would invent cross-record effect paths through
coincidental names such as `value` and `get`. Real AST references still target
their units, all outgoing lexical/dependent-name edges remain, and independently
removable members still participate in reverse member-name matching. Forward
name matching continues to index every member for rules 5 and 6. The model stores
the member spelling separately from namespace-scope indexed names, so real
nonmember names in a unit still participate in reverse effect matching. A unit
test checks that distinction and verifies that a true system-specialization
hook in a mandatory unit continues to refuse.
Protocol 7 rejects older parent/worker models: although the field layout is
unchanged, member spelling and ownership now have different semantics. A valid
version-6 payload is explicitly rejected by the protocol regression test.

This is sound because the owner and these units cannot be removed separately.
Their combined text and real dependencies are the corresponding retained part
of D3d-7's whole record. The members omitted from that component are precisely
the independently removable units, which still undergo D3e's name/AST retention
and potential-effect analysis, including conservative additional name edges.
Every unseen library call to a mandatory member still has the same retained
type carrier, definition and effect dependencies as in D3d-7. A12's inventory
continues to protect all library-spelled members, including dormant protocols;
the change avoids adding a second, unbounded hook solely at an edit boundary.

Separately, `memberOf` now supplies forward ownership retention directly, instead
of storing an artificial AST edge in `edges`. Ownership alone is not a source
instantiation dependency: removing a method cannot remove its enclosing record
definition. Leaving this artificial edge in the reverse graph retained *every*
ordinary member of an effectful owner. Actual type, AST, lexical, dependent-name,
union and system dependencies remain in that graph, including references to
the owner's static storage from a member's signature or body.

The `member_owner_dispatch` prune case has an effectful template static inline
member, constructors used through `std::vector`, a destructor, conversion and
operator, plus an unused ordinary function that must be removed. It reproduces
the refusals on the pre-fix worker and compares original/pruned output with GCC
and Clang. Focused cases also cover virtual/nonvirtual overload aggregation,
rule 8 and a dormant library-only allocator protocol whose entire effect path
must survive while its unrelated ordinary member is removed. The namespace
constant fixture now exercises its original runtime oracle with A12 enabled,
instead of expecting the spurious constructor/destructor refusal.

The include-graph audit covered all **68 ATL examples** at `97d9e23`. Only
`modular-arithmetic/residues` includes `dynamic_mod_int`; the other mutable static
storage found in `counting_sort` and `splay_engine` is block-scope storage excluded
from effect seeding by A11. A productized spot check of **27 other examples**
found no unbounded-system-effect refusal. All **54 GCC/Clang runs** matched the
examples' expected output. Twenty-six artifacts were pruned; `fast-io/roundtrip`
fell back on an unresolved user conditional in `reader.hpp:88`.

With the final worker, `modular-arithmetic/residues` is **pruned to 40,265 bytes**
using `--strip-comments --no-line-markers --judge cses`, C++23, `-O2`, and default
A11/A12. The marker-free validation passes and the artifact contains no `#line`.
Standalone builds with GCC 15.2 and Clang 22.1.8 targeting x86_64 both run under
QEMU and match `residues.out` byte for byte. The final generation, compilation and
run deadlines were 120, 60 and 30 seconds respectively. Builds reused the same
read-only LLVM 23.1.5 SDK through a symlink, with Clang 22, `-O1 -DNDEBUG`, LLD
and at most `-j2`.

The nine Python suites passed **293 tests** (209 unittest, 84 library pytest).
The library tests ran as 83 tests plus the large fourteen-build fixture in
isolation to fit the container's 3 GiB memory limit. Test assertions are unchanged.
The compiler packages were extracted within the worktree after checking their
SHA-256 values against Ubuntu's package indexes; the original SDK and ATL clone
were read-only. Local evidence is in `measurements/owner-dispatch/` (including
`sweep/summary.json` and `static-inline-consumers.json`) and `.checks/`.

All **104 CTest checks** passed across the final full run and an isolated rerun,
including all 81 caide cases. The full run passed 103 checks; the aggregate
`pipeline_cli_d3c` exceeded its 120-second deadline while another CTest was
running. It passed alone in **82.25 seconds**, with a temporary build-directory
180-second hard cap. Repository test deadlines and assertions are unchanged.
`resolver_differential` used the same generated-file cross-GCC oracle adjustment
described below for the native host's unsigned `wchar_t`. All 36 focused pruning
unit tests pass, including real nonmember-hook preservation and protocol-6
rejection. `git diff --check` passes.

## Independent review (2026-11-20)

Both P1 findings were reproduced at `8ff6ccb`: the worker pruned `hidden_hook`
from the dependent using example, and pruned `trigger` from the declaration-effect
example with `--no-assume=instantiation-effects`. Both rendered programs compiled
on the eight-byte-long worker and passed the rendering/static-name invariants.
Selecting the otherwise discarded branch in both originals succeeded with GCC 15
and Clang; the baseline rendered programs respectively failed with a missing
member and an undefined `probe`. This branch-selection oracle models the reported
judge difference without claiming to emulate a complete four-byte-long ABI.
An additional Clang i386 compile/link check uses an actual four-byte `long`:
both originals and fixed CLI artifacts link with `-nostdlib -Wl,-e,main`, while
both baseline artifacts fail with the same missing-member/undefined-symbol
diagnostics. These freestanding link checks are not runtime checks.

Extraction now records expanded `UnresolvedUsingValueDecl` and
`UnresolvedUsingTypenameDecl` names in `dependentNames`, shared by forward
retention and reverse potential effects. The audit also reproduced the omission
for qualified dependent friends (`friend void T::JOIN(hidden_,hook)();`); qualified
dependent function declarations now record their names too. Overloaded, packed
and member-template using declarations have regressions. Typename using targets
and enums remain structural; C++ using-enum cannot name a dependent enum type.
The valid nondependent using-enum case is covered. Dependent bases and expressions
in template-header decltype, requires-clauses and default template arguments are
already reached by recursive AST traversal and pass pasted-name regressions.

The reverse effect graph now has owner-to-every-member edges, restoring the
whole-record potential path through ordinary declarations without adding forward
retention edges. Parameter/return types, default arguments, noexcept-specifiers,
trailing requires and member-template signatures/defaults have regressions with
macro-pasted source names, with and without A11. These are conservative potential
paths, including constructs whose actual substitution is deferred until use.
Unrelated ordinary members still disappear. The exact friend-injection example
remains an A11 exclusion in default mode and preserves its trigger with A11 off;
static-storage effect variants exercise the edges in both modes.

Remeasured all 16 examples in **Productized measurement** at the same pinned ATL
revision and with the same CSES flags, C++23, `-O2` and default A11/A12. Every row
has **Δ 0 bytes** against the productized column; the total remains **701,081
bytes**. All artifacts are pruned with member pruning enabled and contain no
`#line`. Standalone GCC 15.2 and Clang 22.1.8 x86_64 builds pass all **32**
byte-exact output checks under QEMU. Per-example generation/compilation/execution
caps were 300/180/60 seconds, with a 1,500-second sweep cap and 2,500,000 KiB
virtual-memory limits. Evidence is in `measurements/independent-review/`
(`summary.tsv`, `summary.json`, sources, reports and compiler/run logs).

Validation reused the checksum-verified LLVM 23.1.5 SDK through a read-only
symlink, with Clang 22, `-O1 -DNDEBUG`, LLD, `-j2` and hard deadlines. All **105
CTest checks** pass across the full run and a targeted rerun: the full run passed
104 checks; ten existing fixtures needed eleven owner-provenance assertions
updated from forward ownership to reverse instantiation effects. The rerun passes
all 84 extraction fixture runs. The new independent-review suite passes **40
cases**, and all nine Python suites pass **293 tests** (209 unittest, 84 pytest).
As before, only the generated `resolver_differential` test environment selects a
worktree-local cross-GCC oracle for the host's unsigned `wchar_t`; source and
assertions are unchanged. `git diff --check` passes. Build/test, baseline failure
and i386 evidence remain in `.checks/`; downloaded compiler packages were
SHA-256 checked against Ubuntu's package indexes and extracted inside this worktree.

## Original productization validation

The previously downloaded, checksum-verified LLVM 23.1.5 SDK was reused through
an ignored `spike-work/sdk` symlink; the original SDK was not modified. Builds
used Clang 22.1.8, `-O1 -DNDEBUG`, LLD 22, at most `-j2`, and hard timeouts.

The full **104-test CTest suite** was exercised with a 900-second overall deadline
and individual deadlines of at most 120 seconds. The initial run passed 102 tests;
after refreshing whole-record expectations, both affected tests passed on rerun.
This includes all 81 caide cases, **79 extraction cases**, and **33 focused member
cases** covering extraction, rendering/invariants, GCC 15.2 and Clang 22.1.8
compilation/runtime, and expected effect refusals. New regressions cover inactive
and unused paste macros, pasted dependent names in uninstantiated system templates,
system macros invoked with user arguments (including expanded arguments), system
pasting at user call sites, and user macros invoked inside system templates.

The original productization's instantiation-effect expectations checked the
effect-bearing member and its retained owner separately. Before the owner-dispatch
fix above, the namespace-constant fixture refused
unbounded constructor/destructor effects under member pruning; its A12 opt-out
variant preserves the whole-record runtime comparison. Refusal cases validate
the original program's runtime oracle without trying to render a refused result.

The nine Python suites pass **293 tests** (209 unittest and 84 pytest): extensions,
profiles, drift, release, retry, matrix, LLVM tooling, measurement tooling and
library tooling. Each suite has a 120-second timeout, except library pytest's
300-second timeout. Missing pytest was installed only in ignored `.checks/python`.
`git diff --check` passes.

This AArch64 host's native GCC cannot represent the synthetic signed-`wchar_t`
model in `resolver_differential`. Only that test's generated build-directory
`ENVIRONMENT` selects the worktree-local x86_64-targeting GCC 15.2 oracle; its
assertions and source are unchanged. Public Ubuntu cross-toolchain/QEMU packages
were downloaded, checked against package-index SHA-256 values, and extracted
inside ignored `.checks/cross`; versions and hashes are in `packages.json` there.
All other CTests use native compilers.

The before/after `<cstdint>` check confirmed that system `##` originally disabled
member pruning. With the fix, both `<cstdint>` and `<bits/stdc++.h>` leave it enabled
and remove the unused method from a retained record.

## Productized measurement

Measured locally after the inventory fix on the same 16 ATL examples as the
prototype V1 column, using the read-only ATL clone and CSES nlm conventions:
`--strip-comments --no-line-markers --judge cses`, C++23, `-O2`, default A11/A12.
Worker library headers came from GCC 15.2; no CSES header capture was used, as
in the prototype. All 16 artifacts are pruned with member pruning enabled.

Every generated source compiled standalone, without the ATL include directory,
with both GCC 15.2 and Clang 22.1.8 targeting x86_64. All **32 runs** under QEMU
matched the examples’ expected output byte for byte (using their `.in` files
when present). Generation had a 300-second timeout per example, compilation
180 seconds per compiler, execution 60 seconds, and the sweep 1,500 seconds;
processes were limited to 2,500,000 KiB of virtual memory. These are local
toolchain checks under A12, rather than validation on the CSES judge itself.

| Example | Prototype V1 bytes | Productized bytes | Δ bytes |
| --- | ---: | ---: | ---: |
| euler-tour-tree/basic | 62,199 | 62,199 | 0 |
| link-cut-tree/basic | 83,599 | 83,599 | 0 |
| subtree-link-cut-tree/basic | 89,693 | 89,693 | 0 |
| top-tree/basic | 123,610 | 123,610 | 0 |
| splay-engine/quick-start | 12,809 | 12,809 | 0 |
| splay-map/quick-start | 44,589 | 44,589 | 0 |
| splay-sequence/quick-start | 42,780 | 42,780 | 0 |
| treap-map/quick-start | 32,524 | 32,524 | 0 |
| treap-sequence/quick-start | 30,887 | 30,887 | 0 |
| dynamic-segment-tree/quick-start | 6,539 | 6,539 | 0 |
| dynamic-lazy-segment-tree/quick-start | 10,444 | 10,444 | 0 |
| meldable-heap-forest/basic | 12,244 | 12,244 | 0 |
| convolution/quick | 69,770 | 69,770 | 0 |
| coordinate-compression/move-only | 19,418 | 19,418 | 0 |
| matrix/quick | 51,273 | 51,454 | +181 |
| xor-basis/queries | 8,522 | 8,522 | 0 |
| **Total** | **700,900** | **701,081** | **+181** |

Fifteen byte counts match V1. `matrix/quick` is 181 bytes larger: comparison with
the prototype source shows that productization preserves its structural friend
`operator==` and adjacent whitespace. Both versions remove 32 member units; this
difference does not come from retaining an additional macro-produced member name.

Sources, per-case reports, compiler/run logs, captured outputs and the machine
readable summary remain locally in ignored `measurements/member-productized/`
(`summary.tsv` and `summary.json`). The local harness is
`.checks/measure-productized.py`; public cross-toolchain package versions and
SHA-256 values are recorded in `.checks/cross/packages.json`.

## Prototype measurement

These tables are historical measurements of the unreviewed prototype, **not
measurements of adopted D3e**. Prototype V1 did not collect expanded library
identifiers; its gains must not be advertised as production gains. V2 is a
historical comparison and no longer implemented. The obsolete variant harness
`ci/atl-sweep/member-experiment.py` was removed. The local adopted-policy
comparison is recorded under Productized measurement.

The 16 examples passed standalone compilation and byte-exact output with GCC 15.2
and Clang 22.1.8 on the prototype, using the sweep's nlm convention
(`--strip-comments --no-line-markers --judge cses`, C++23, `-O2`). Measurements used
x86_64 cross compilers with local GCC 15 headers and QEMU on AArch64. V1 did not
capture CSES headers. All used default A11. The baseline was D3d-7; V1/V2 retain
their original meanings. These results establish only the tested examples under
those local toolchains, not A12 soundness for all libraries or judges.

| Example | D3d-7 bytes | V1 bytes | V2 bytes | V1 gain | V2 gain |
| --- | ---: | ---: | ---: | ---: | ---: |
| euler-tour-tree/basic | 66,277 | 62,199 | 61,049 | 6.2% | 7.9% |
| link-cut-tree/basic | 94,778 | 83,599 | 81,779 | 11.8% | 13.7% |
| subtree-link-cut-tree/basic | 95,600 | 89,693 | 88,707 | 6.2% | 7.2% |
| top-tree/basic | 136,086 | 123,610 | 122,624 | 9.2% | 9.9% |
| splay-engine/quick-start | 16,778 | 12,809 | 12,536 | 23.7% | 25.3% |
| splay-map/quick-start | 47,730 | 44,589 | 43,796 | 6.6% | 8.2% |
| splay-sequence/quick-start | 46,091 | 42,780 | 41,821 | 7.2% | 9.3% |
| treap-map/quick-start | 40,304 | 32,524 | 30,638 | 19.3% | 24.0% |
| treap-sequence/quick-start | 39,598 | 30,887 | 29,380 | 22.0% | 25.8% |
| dynamic-segment-tree/quick-start | 8,931 | 6,539 | 6,539 | 26.8% | 26.8% |
| dynamic-lazy-segment-tree/quick-start | 11,982 | 10,444 | 10,284 | 12.8% | 14.2% |
| meldable-heap-forest/basic | 12,711 | 12,244 | 12,387 | 3.7% | 2.5% |
| convolution/quick | 69,953 | 69,770 | 69,770 | 0.3% | 0.3% |
| coordinate-compression/move-only | 19,418 | 19,418 | 19,418 | 0.0% | 0.0% |
| matrix/quick | 53,035 | 51,273 | 51,303 | 3.3% | 3.3% |
| xor-basis/queries | 10,129 | 8,522 | 7,982 | 15.9% | 21.2% |

| Example | V1 top blockers (retained bytes) | V2 top blockers (retained bytes) |
| --- | --- | --- |
| euler-tour-tree/basic | operator() (rule 1): 4,066; assign_parents (rule 5): 2,078; euler_tour_tree (rule 5): 1,935 | operator() (rule 1): 4,066; assign_parents (rule 5): 2,078; euler_tour_tree (rule 5): 1,935 |
| link-cut-tree/basic | link_cut_tree_impl (rule 1): 5,879; operator() (rule 1): 3,947; descend (rule 5): 2,231 | link_cut_tree_impl (rule 1): 5,879; operator() (rule 1): 3,947; descend (rule 5): 2,231 |
| subtree-link-cut-tree/basic | link_cut_tree_impl (rule 1): 5,879; operator() (rule 1): 3,872; descend (rule 5): 2,231 | link_cut_tree_impl (rule 1): 5,879; operator() (rule 1): 3,872; descend (rule 5): 2,231 |
| top-tree/basic | top_tree_impl (rule 1): 9,136; operator() (rule 1): 4,930; operator() (rule 6): 2,520 | top_tree_impl (rule 1): 9,136; operator() (rule 1): 7,450; descend (rule 5): 2,231 |
| splay-engine/quick-start | descend (rule 5): 2,231; for_each (rule 5): 710; splay_at (rule 6): 436 | descend (rule 5): 2,231; for_each (rule 5): 710; splay_at (rule 6): 436 |
| splay-map/quick-start | descend (rule 5): 2,231; splay_map_impl (rule 5): 1,728; for_each (rule 4): 1,717 | descend (rule 5): 2,231; splay_map_impl (rule 5): 1,728; for_each (rule 5): 1,717 |
| splay-sequence/quick-start | splay_sequence_impl (rule 5): 3,685; descend (rule 5): 2,231; build_balanced (rule 5): 1,300 | splay_sequence_impl (rule 5): 3,685; descend (rule 5): 2,231; build_balanced (rule 5): 1,300 |
| treap-map/quick-start | treap_map (rule 5): 1,680; build_sorted (rule 5): 1,353; upsert (rule 5): 1,234 | treap_map (rule 5): 1,680; build_sorted (rule 5): 1,353; upsert (rule 5): 1,234 |
| treap-sequence/quick-start | treap_sequence (rule 6): 3,625; fold_range (rule 5): 1,667; at (rule 4): 1,225 | treap_sequence (rule 6): 3,625; fold_range (rule 5): 1,667; at (rule 4): 1,225 |
| dynamic-segment-tree/quick-start | point_update (rule 5): 1,074; fold (rule 5): 1,008; at (rule 4): 500 | point_update (rule 5): 1,074; fold (rule 5): 1,008; at (rule 4): 500 |
| dynamic-lazy-segment-tree/quick-start | fold (rule 5): 973; dynamic_lazy_segment_tree (rule 5): 889; apply_rec (rule 5): 684 | fold (rule 5): 973; dynamic_lazy_segment_tree (rule 5): 889; apply_rec (rule 5): 684 |
| meldable-heap-forest/basic | pair_up (rule 5): 732; meldable_heap_forest (rule 1): 653; allocate (rule 4): 563 | pair_up (rule 5): 732; meldable_heap_forest (rule 1): 653; allocate (rule 4): 563 |
| convolution/quick | forward_with (rule 5): 909; inverse_with (rule 5): 886; reduce (rule 4): 867 | mul (rule 5): 917; forward_with (rule 5): 909; inverse_with (rule 5): 886 |
| coordinate-compression/move-only | ranks (rule 5): 1,987; coordinate_compression (rule 5): 1,295; contains (rule 4): 213 | ranks (rule 5): 1,987; coordinate_compression (rule 5): 1,295; contains (rule 4): 213 |
| matrix/quick | operator+= (rule 1): 1,560; mul (rule 5): 836; matrix (rule 5): 735 | operator+= (rule 1): 1,560; mul (rule 5): 836; matrix (rule 5): 735 |
| xor-basis/queries | insert (rule 4): 872; max_xor (rule 5): 347; rank (rule 4): 315 | insert (rule 4): 872; max_xor (rule 5): 347; contains (rule 4): 311 |
