<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Remaining output bytes: ATL trim analysis

2026-11-22. Analysis only; base `49d6b76`, branch `trim-analysis`. Read-only ATL input `4cbbe2f`: 126 examples. No production policy change.

**Completed: 126/126 examples, 4,905,420 output bytes.** Final accounting reconciles every artifact byte exactly.

## Measurement and byte attribution

CSES profile `cses-gcc13-cpp23`, C++23, `-O2 --strip-comments --no-line-markers`, default A11/A12, following `ci/atl-sweep/sweep.sh`. Totals sum example occurrences; shared ATL headers are counted in each output. The worker uses GCC 15.2 x86_64 headers, not a CSES GCC 13.3 header capture. These are local checks, not judge runs.

The diagnostic preserves production retention. The baseline comparison has identical coordinate-compression output bytes and family/member report rows. Whole-output categories use the renderer copy map into the immutable comment-stripped worker input. Every final byte is assigned once, including indentation, newlines, directives and renderer-inserted deletion separators. Declaration-owned bytes and output-category bytes are separate denominators: output categories include adjacent trivia and carve out requires clauses.

The example's own non-directive source takes precedence. Directives own their entire physical lines. Nested data, aliases, enums and assertions are carved out of structural records; function-local declarations remain part of their function. Function/member ranges otherwise include signatures, bodies and template headers. Explicit requires-expression and head/trailing-clause spans are separated. Unowned line indentation inherits that line's declaration category; blank structural gaps and inserted separators remain Other. Inactive text in unresolved flat branches is opaque when the worker did not parse it.

“No AST reference” means no positive surviving semantic witness, not proven dead code. Member units exclude self-group references; rule-1 declarations use their own declaration identities. Data and aliases use their exact declaration reference keys. A resolved unevaluated function reference counts as reached in the whole-output table even though it is evidence (2) in the separate rule-1 table; trait- and flag-only evidence does not. Roots such as `main` may legitimately have no incoming reference. Evidence from reached system entities counts; references from discarded physical text do not.

## Whole-output totals

| Category | Output bytes | % | No positive AST-reference bytes | Why unreferenced bytes remain |
| --- | ---: | ---: | ---: | --- |
| Example's own code | 64,876 | 1.32% | 47,096 | Entry point / enclosing example declaration; source is part of the program. |
| Preprocessor directives and includes | 226,542 | 4.62% | 226,542 | Source directives are never removed; includes and generated guards remain. |
| Namespace and record scaffolding | 441,465 | 9.00% | 51,569 | Structural family ownership, class layout and namespace/access syntax. |
| Data members | 97,452 | 1.99% | 12,339 | D3e preserves all non-function members of a kept record; layout/initialization may observe them. |
| Member functions: non-rule-1 | 995,147 | 20.29% | 304,189 | A12, dependent/name rules, language lookup, roots/pins or refusal; see the retention table. |
| ctor-explicit | 63,409 | 1.29% | 16,119 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| ctor-implicit-converting | 5,436 | 0.11% | 2,359 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| multi-arg ctor | 84,590 | 1.72% | 42,866 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| default ctor | 21,476 | 0.44% | 6,212 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| copy/move ctor | 1,898 | 0.04% | 1,277 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| destructor | 348 | 0.01% | 348 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| copy/move assignment | 3,146 | 0.06% | 2,786 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| operator() | 77,425 | 1.58% | 50,531 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| operator-comparison | 7,559 | 0.15% | 5,575 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| operator-arithmetic | 40,606 | 0.83% | 25,893 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| operator-stream | 0 | 0.00% | 0 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| operator-other | 15,104 | 0.31% | 9,912 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| conversion-explicit | 0 | 0.00% | 0 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| conversion-implicit | 0 | 0.00% | 0 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| deduction guide | 26,274 | 0.54% | 26,274 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| virtual | 0 | 0.00% | 0 | D3e rule 1 keeps the declaration with its record; overload/trait/target sensitivity is not disproved. |
| Free functions and function templates | 1,734,051 | 35.35% | 492,025 | A6/ADL, nonmember operators, lexical/dependent closure, root/pin or flat refusal. |
| Type aliases and using-declarations | 160,152 | 3.26% | 53,501 | Structural record ownership, namespace-using protection or lexical/dependent closure. |
| Variables and constants | 109,165 | 2.23% | 64,875 | Structural ownership, non-inert storage/effect roots, references/names or flat refusal. |
| static_asserts | 7,015 | 0.14% | 7,015 | Namespace static_assert is a root; record assertions stay structurally. |
| Concepts and requires clauses | 449,290 | 9.16% | 62,143 | Enclosing retained declaration or constraint/lexical dependency; requires can inspect API without a call. |
| Enums | 6,284 | 0.13% | 0 | Structural record ownership or enum-family/name/lexical dependency. |
| Other | 266,710 | 5.44% | 266,710 | Blank structural gaps, renderer separators, pinned unknown declarations and unparsed flat-branch text. |
| **Total** | **4,905,420** | **100%** | **1,778,156** | Every output byte appears exactly once. |

### Top 15 examples by output bytes

Each cell is bytes. Rule 1 combines its declaration-kind rows above; “Rest” combines aliases, variables, assertions, constraints, enums and Other.

| Example | Output | Own | PP | Scaffold | Data | Ordinary members | Rule 1 | Free | Rest | No AST | Artifact |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| delaunay-quick | 190,738 | 364 | 3,478 | 9,357 | 2,158 | 16,733 | 11,810 | 119,315 | 27,523 | 125,048 | flat |
| convex-polygon-operations-quick | 130,778 | 477 | 2,292 | 6,873 | 1,502 | 7,744 | 7,404 | 84,119 | 20,367 | 57,289 | flat |
| point-location-quick | 128,135 | 552 | 3,334 | 9,863 | 2,256 | 34,219 | 13,783 | 32,985 | 31,143 | 55,531 | flat |
| series-composition-quick | 124,249 | 901 | 4,608 | 7,096 | 1,725 | 13,996 | 3,150 | 79,761 | 13,012 | 26,402 | pruned |
| top-tree-basic | 124,169 | 674 | 2,626 | 12,381 | 3,246 | 50,367 | 25,473 | 5,282 | 24,120 | 37,794 | pruned |
| polygon-overlay-quick | 108,482 | 464 | 2,921 | 8,862 | 1,839 | 20,736 | 9,283 | 35,555 | 28,822 | 46,472 | flat |
| polynomial-points-quick | 105,055 | 935 | 4,180 | 7,405 | 1,952 | 21,889 | 3,843 | 53,116 | 11,735 | 24,430 | pruned |
| arrangement-quick | 102,172 | 356 | 2,789 | 8,716 | 1,711 | 20,736 | 9,283 | 30,889 | 27,692 | 46,025 | flat |
| power-series-powers-and-roots | 101,034 | 1,026 | 3,794 | 6,998 | 1,615 | 12,194 | 3,062 | 60,339 | 12,006 | 23,234 | pruned |
| circle-quick | 100,041 | 416 | 2,353 | 6,933 | 550 | 5,048 | 4,400 | 59,617 | 20,724 | 48,290 | flat |
| polynomial-gcd-quick | 99,843 | 933 | 4,348 | 7,186 | 1,795 | 12,194 | 3,062 | 58,062 | 12,263 | 24,863 | pruned |
| triangulation-quick | 99,184 | 511 | 2,510 | 7,330 | 627 | 13,209 | 6,282 | 43,031 | 25,684 | 47,233 | flat |
| polynomial-gcd-pade | 98,064 | 732 | 4,367 | 7,276 | 1,867 | 12,194 | 3,062 | 56,704 | 11,862 | 24,762 | pruned |
| halfplane-intersection-quick | 96,791 | 492 | 1,995 | 5,887 | 609 | 7,298 | 894 | 58,633 | 20,983 | 49,000 | flat |
| calipers-quick | 90,936 | 652 | 1,802 | 6,389 | 1,322 | 6,837 | 6,643 | 50,273 | 17,018 | 45,013 | flat |

## Member retention, previous and current

Units group `(union-find family group, member spelling)` and include overloads/redeclarations. Rule 1 is excluded. Classification follows [the preceding retention analysis](2026-11-21-member-call-analysis.md): refusal; positive AST; independent mandatory roots/protection; A12; dependent sites; foreign resolved bindings; macro-only; opaque remainder. R-dep(b) is part of R-AST and zero in this exclusive table. A name inventory is independent protection, even when unrelated user tokens also match.

R-dep(a) requires a resolved foreign target at every matching kept physical site; missing/unobserved sites and unresolved declaration sites remain (c). R-cycle audits whether AST-only users are themselves locally unused collision units. R-other includes opaque/mixed raw-token bindings; unresolved macro-argument joins are conservatively left there. The previous table and this table share definitions; the extractor/classifier implementation is fresh, so unresolved evidence may move from a narrower prior row to R-other.

| Class | Previous 85 units / bytes | Current units / bytes | Delta bytes |
| --- | ---: | ---: | ---: |
| R-AST | 1,863 / 542,969 | 2,118 / 626,218 | +83,249 |
| R-lib | 801 / 164,127 | 917 / 179,509 | +15,382 |
| R-dep(a) | 23 / 2,095 | 26 / 2,565 | +470 |
| R-dep(b) | 0 / 0 | 0 / 0 | +0 |
| R-dep(c) | 221 / 45,852 | 229 / 47,048 | +1,196 |
| R-nondep-other | 18 / 6,387 | 21 / 6,827 | +440 |
| R-macro | 0 / 0 | 0 / 0 | +0 |
| R-cycle | 0 / 0 | 0 / 0 | +0 |
| R-other | 160 / 26,419 | 800 / 206,001 | +179,582 |

Ordinary member owned bytes: **1,068,168**. Rule-1 member units: **1,200 / 353,221**, compared with **838 / 237,583** previously.

## Rule 1: declaration kinds and evidence

Each cell is written declarations / owned bytes. Implicit compiler-generated declarations and repeated template instances do not add counts. Default and copy/move constructors take precedence over explicitness. Explicit and implicit converting rows mean callable with one argument (at most one required, at least one accepted); the multi-argument row has more than one required argument. Lambda call operators nested inside a unit belong to the enclosing written unit, and are not counted as another declaration.

(1) has a resolved reference/call from surviving user text or a system entity reached from kept code. (2), with no (1), has an unevaluated reference, a relevant constructibility/assignment/conversion or structural special-member trait involving its record, CTAD participation, or residual compiler used/referenced flags. Relevant trait records conservatively protect every matching kind of declaration of the tested record: this is a broader group (2) than the preceding overload-selection replay, and shrinks the candidate set. Flags can originate in discarded code. (3) has none of those observed witnesses; it is a worker candidate, not a cross-target deletion proof.

Guides are additional record-owned declarations, outside the old member-unit denominator. Virtualness is recorded independently; virtual destructors remain in the destructor row. Stream operators here are member operators only; free/hidden-friend stream operators are counted as free functions.

| Kind | (1) Resolved AST | (2) Trait / unevaluated / residual flags | (3) No reference | Total |
| --- | ---: | ---: | ---: | ---: |
| ctor-explicit | 132 / 51,554 | 18 / 4,697 | 78 / 18,633 | 228 / 74,884 |
| ctor-implicit-converting | 23 / 3,077 | 2 / 506 | 17 / 1,853 | 42 / 5,436 |
| multi-arg ctor | 120 / 47,208 | 28 / 9,225 | 99 / 43,451 | 247 / 99,884 |
| default ctor | 175 / 17,181 | 51 / 3,417 | 115 / 5,057 | 341 / 25,655 |
| copy/move ctor | 6 / 652 | 8 / 582 | 10 / 731 | 24 / 1,965 |
| destructor | 0 / 0 | 6 / 348 | 0 / 0 | 6 / 348 |
| copy/move assignment | 1 / 360 | 8 / 1,040 | 13 / 1,836 | 22 / 3,236 |
| operator() | 128 / 27,789 | 1 / 109 | 168 / 50,550 | 297 / 78,448 |
| operator-comparison | 22 / 1,863 | 6 / 516 | 67 / 5,227 | 95 / 7,606 |
| operator-arithmetic | 104 / 14,713 | 24 / 2,479 | 234 / 23,463 | 362 / 40,655 |
| operator-stream | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| operator-other | 53 / 5,192 | 0 / 0 | 79 / 9,912 | 132 / 15,104 |
| conversion-explicit | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| conversion-implicit | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| deduction guide | 0 / 0 | 27 / 5,278 | 103 / 24,498 | 130 / 29,776 |
| virtual | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |

**Explicit one-argument constructors, evidence (3): 78 / 18,633. Implicit one-argument converting constructors, evidence (3): 17 / 1,853. All rule-1 kinds plus guides, evidence (3): 983 / 185,211.**

### Explicit constructor candidates

Identical written source declarations are grouped across examples; bytes below sum occurrences. The full source-declaration/evidence inventory is included in the appendix below.

| Written declaration | Examples / occurrences | Candidate bytes |
| --- | ---: | ---: |
| `atl/ds/static_vector.hpp:88` `atl::static_vector::static_vector<T, Capacity>` | 18 | 6,174 |
| `atl/ds/flat_hash_map.hpp:80` `atl::flat_hash_map::flat_hash_map<Key, T, Hash, Eq>` | 11 | 2,233 |
| `atl/ds/coordinate_compression.hpp:182` `atl::coordinate_compression::coordinate_compression<T, Compare>` | 2 | 1,810 |
| `atl/ds/persistent_list.hpp:225` `atl::persistent_list::persistent_list<T>` | 2 | 1,288 |
| `atl/ds/flat_hash_map.hpp:71` `atl::flat_hash_map::flat_hash_map<Key, T, Hash, Eq>` | 10 | 940 |
| `atl/ds/detail/hash_table.hpp:114` `atl::detail::hash_table::hash_table<Slot, Key, KeyOf, Hash, Eq>` | 10 | 920 |
| `atl/nt/detail/online_convolution.hpp:194` `atl::detail::online_product_state::online_product_state<Coefficient, Engine, Algorithm, IsFixed>` | 1 | 866 |
| `atl/nt/montgomery.hpp:260` `atl::detail::montgomery64::montgomery64` | 2 | 542 |
| `atl/ds/binary_indexed_tree.hpp:226` `atl::binary_indexed_tree::binary_indexed_tree<T, Dim, Op>` | 2 | 404 |
| `atl/ds/splay_sequence.hpp:611` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | 1 | 315 |
| `atl/ds/persistent_heap.hpp:319` `atl::persistent_heap::persistent_heap<T, Compare>` | 1 | 304 |
| `atl/ds/weighted_dsu.hpp:156` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | 1 | 263 |
| `atl/ds/persistent_vector.hpp:1284` `atl::semi_persistent_vector::semi_persistent_vector<T, Radix>` | 1 | 257 |
| `atl/ds/flat_hash_set.hpp:72` `atl::flat_hash_set::flat_hash_set<Key, Hash, Eq>` | 1 | 243 |
| `atl/ds/binary_indexed_tree.hpp:217` `atl::binary_indexed_tree::binary_indexed_tree<T, Dim, Op>` | 1 | 231 |
| `atl/ds/top_tree.hpp:1664` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | 1 | 225 |
| `atl/ds/persistent_list.hpp:240` `atl::persistent_list::persistent_list<T>` | 2 | 224 |
| `atl/ds/top_tree.hpp:1669` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | 1 | 221 |
| `atl/ds/segment_tree_beats.hpp:1455` `atl::beats_segment_tree::beats_segment_tree<Element, Sum>` | 1 | 202 |
| `atl/ds/detail/persistent_arena.hpp:38` `atl::detail::persistent_arena::persistent_arena<Node>` | 2 | 190 |
| `atl/ds/persistent_vector.hpp:1292` `atl::semi_persistent_vector::semi_persistent_vector<T, Radix>` | 1 | 129 |
| `atl/ds/link_cut_tree.hpp:852` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | 1 | 126 |
| `atl/ds/treap_map.hpp:366` `atl::treap_map::treap_map<Key, Value, Compare, Op>` | 1 | 119 |
| `atl/ds/persistent_vector.hpp:982` `atl::persistent_vector::persistent_vector<T, Radix>` | 1 | 116 |
| `atl/nt/modular.hpp:123` `atl::barrett::barrett` | 1 | 107 |
| `atl/ds/flat_hash_set.hpp:63` `atl::flat_hash_set::flat_hash_set<Key, Hash, Eq>` | 1 | 94 |
| `atl/ds/persistent_heap.hpp:310` `atl::persistent_heap::persistent_heap<T, Compare>` | 1 | 90 |

### Implicit converting constructor candidates

The source audit does **not** confirm that every implicit-(3) candidate has an explicit doc-comment exception. In `atl/ds/static_vector.hpp:74`, `static_vector(std::initializer_list<T>)` is implicit. Its comment says “A vector holding the elements of init, in order” and gives the capacity precondition, but does not state why implicit conversion is intentional. This is list-value construction, yet the exception is not expressly documented in the supplied `4cbbe2f` clone. Other implicit candidate declarations, if present, are listed below; the read-only clone was not changed.

| Written declaration | Examples / occurrences | Candidate bytes |
| --- | ---: | ---: |
| `atl/ds/static_vector.hpp:74` `atl::static_vector::static_vector<T, Capacity>` | 17 | 1,853 |

## A12 inventory detail

Exclusive provenance is (i) an included worker header’s raw identifier, then (ii) an actual system-macro expansion only, then (iii) scanned-build-only. A name can occur in multiple sources; the table assigns the strongest local source. It covers only R-lib bytes after positive AST and independent mandatory rules. Scanned inventory: 14 builds, generator 4, 32,555 names.

| Name | Units | (i) Worker headers | (ii) Macro only | (iii) Scanned only |
| --- | ---: | ---: | ---: | ---: |
| `at` | 23 | 11,129 | 0 | 0 |
| `power` | 51 | 0 | 0 | 9,580 |
| `erase` | 20 | 8,569 | 0 | 0 |
| `reserve` | 51 | 8,050 | 0 | 0 |
| `for_each` | 12 | 7,839 | 0 | 0 |
| `find` | 15 | 7,178 | 0 | 0 |
| `resize` | 16 | 6,816 | 0 | 0 |
| `size` | 90 | 6,027 | 0 | 0 |
| `contains` | 23 | 5,815 | 0 | 0 |
| `emplace_back` | 22 | 5,076 | 0 | 0 |
| `push_back` | 20 | 4,801 | 0 | 0 |
| `emplace` | 14 | 4,650 | 0 | 0 |
| `mul` | 19 | 2,928 | 0 | 1,708 |
| `empty` | 78 | 4,492 | 0 | 0 |
| `try_emplace` | 7 | 4,228 | 0 | 0 |
| `insert` | 6 | 4,094 | 0 | 0 |
| `set` | 17 | 3,974 | 0 | 0 |
| `path` | 3 | 0 | 0 | 3,357 |
| `clear` | 43 | 3,353 | 0 | 0 |
| `back` | 20 | 3,256 | 0 | 0 |
| `unchecked` | 18 | 0 | 0 | 3,248 |
| `front` | 23 | 3,220 | 0 | 0 |
| `count` | 13 | 3,218 | 0 | 0 |
| `pop_back` | 23 | 3,064 | 0 | 0 |
| `apply` | 6 | 2,973 | 0 | 0 |
| `insert_or_assign` | 7 | 2,674 | 0 | 0 |
| `add` | 11 | 828 | 0 | 1,767 |
| `split_at_node` | 3 | 0 | 0 | 2,421 |
| `capacity` | 33 | 2,362 | 0 | 0 |
| `split` | 7 | 2,043 | 0 | 0 |
| `data` | 17 | 2,006 | 0 | 0 |
| `emplace_front` | 6 | 0 | 0 | 1,724 |
| `rotate` | 6 | 1,620 | 0 | 0 |
| `update` | 6 | 0 | 0 | 1,590 |
| `distance` | 6 | 1,544 | 0 | 0 |
| `one` | 22 | 0 | 0 | 1,406 |
| `release` | 5 | 1,344 | 0 | 0 |
| `join` | 4 | 1,340 | 0 | 0 |
| `sub` | 11 | 595 | 0 | 714 |
| `next` | 2 | 1,103 | 0 | 0 |
| `prev` | 2 | 1,101 | 0 | 0 |
| `index` | 8 | 973 | 0 | 77 |
| `reduce` | 3 | 682 | 0 | 341 |
| `detach` | 4 | 0 | 0 | 1,012 |
| `root` | 10 | 0 | 0 | 961 |
| `clone` | 2 | 937 | 0 | 0 |
| `parent` | 6 | 0 | 0 | 893 |
| `reset` | 5 | 772 | 0 | 0 |
| `elements` | 8 | 400 | 0 | 222 |
| `concat` | 3 | 600 | 0 | 0 |
| `identity` | 6 | 535 | 0 | 0 |
| `push_front` | 3 | 534 | 0 | 0 |
| `merge` | 2 | 529 | 0 | 0 |
| `splay` | 3 | 0 | 0 | 513 |
| `depth` | 4 | 0 | 0 | 506 |
| `pop_front` | 4 | 0 | 0 | 469 |
| `value` | 4 | 452 | 0 | 0 |
| `visit` | 1 | 448 | 0 | 0 |
| `rank` | 3 | 425 | 0 | 0 |
| `acquire` | 2 | 0 | 0 | 382 |
| `total` | 2 | 0 | 0 | 347 |
| `word_count` | 3 | 0 | 0 | 322 |
| `pop` | 3 | 298 | 0 | 0 |
| `of` | 3 | 271 | 0 | 0 |
| `right` | 2 | 270 | 0 | 0 |
| `translate` | 2 | 0 | 0 | 268 |
| `left` | 2 | 268 | 0 | 0 |
| `push_range` | 1 | 0 | 0 | 256 |
| `rebind` | 2 | 248 | 0 | 0 |
| `current` | 1 | 232 | 0 | 0 |
| `sizes` | 2 | 230 | 0 | 0 |
| `suffix` | 1 | 227 | 0 | 0 |
| `position` | 2 | 220 | 0 | 0 |
| `vector` | 2 | 210 | 0 | 0 |
| `push` | 2 | 194 | 0 | 0 |
| `is_unique` | 2 | 0 | 0 | 192 |
| `order` | 2 | 0 | 0 | 166 |
| `flip` | 1 | 165 | 0 | 0 |
| `clamp` | 1 | 161 | 0 | 0 |
| `seed` | 4 | 148 | 0 | 0 |
| `top` | 1 | 0 | 0 | 140 |
| `element` | 1 | 0 | 0 | 140 |
| `advance` | 1 | 135 | 0 | 0 |
| `pos` | 2 | 0 | 0 | 132 |
| `symbol` | 1 | 120 | 0 | 0 |
| `bottom` | 1 | 0 | 0 | 116 |
| `length` | 1 | 115 | 0 | 0 |
| `last` | 2 | 105 | 0 | 0 |
| `id` | 1 | 87 | 0 | 0 |
| `max` | 1 | 83 | 0 | 0 |
| `base` | 1 | 70 | 0 | 0 |
| `priority` | 1 | 43 | 0 | 0 |
| `zero` | 1 | 0 | 0 | 42 |

### Inventory counterfactuals

Each counterfactual reruns the complete production closure on the saved model with its embedded scanned-name fallback suppressed in a separate offline copy. Worker-only uses raw included-header names plus actual macro names. The facility estimate instead keeps identifiers in the written bodies/signatures of all system entities transitively reached from the kept TU, including complete reached record definitions, plus actual macro expansions and language lookup names. System-type edges make this a deliberately broad facility proxy. It still misses dormant judge-library paths, alternative overloads and target-selected implementations; it is an estimate, not a sound replacement for A12.

| Counterfactual | Examples with enabled closure | Direct R-lib bytes freed | All owned bytes freed | Rendered output bytes saved |
| --- | ---: | ---: | ---: | ---: |
| Drop scanned-only inventory | 109 | 15,743 | 19,412 | 19,772 |
| Restrict to reached system facilities (estimate) | 109 | 41,289 | 51,506 | 52,248 |

## CI sweep (2026-11-22)

atl-sweep run 45064341637 on main 49d6b76 (release 44829939233, built from 183286f; later commits are docs only), atl `4cbbe2f`, CSES. Baseline: run 44830828469, atl `5e2a6e2`, 85 examples.

| Variant | Examples | Pruned | Run-check failures | Total bytes | Median | Common 85: old → new total |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| plain | 126 | 110 | 0 | 10,583,756 | 66,424 | 6,928,204 → 6,929,973 |
| strip | 126 | 110 | 0 | 5,360,759 | 31,259 | 3,490,480 → 3,490,562 |
| nlm | 126 | 110 | 0 | 4,915,441 | 28,129 | 3,127,312 → 3,127,394 |

The 85 earlier examples are unchanged up to +2 to +12 bytes in 23 outputs (atl source growth). The explicit-constructor convention (atl #21) does not change output bytes on its own, because D3e rule 1 keeps every constructor with its record. The 16 flat outputs are 15 geometry examples (unknown conditional at `include/atl/geometry/predicates.hpp:38`, `#if FLT_EVAL_METHOD`) and `aho-corasick-quick` (unowned user dependency `__builtin_prefetch` at `include/atl/utils/prefetch.hpp:27`); together they are 1,372,694 nlm bytes, 28% of the total.

## Ranked recommendation

The ranking below is by measured/estimated byte opportunity across this 126-example corpus, not by safety or implementation effort. Formatting is the first low-risk implementation choice even if a speculative semantic pool is larger. Gains overlap and must not be added. Gate-waiver and facility values are counterfactual estimates; rule-1/name values are direct owned-byte candidate pools, not approved savings.

| Rank | Rule | Bytes across corpus | Measurement |
| ---: | --- | ---: | --- |
| 1 | Certify the new refusal gates | 405,166 | Offline gate-waiver estimate over 16 affected flat outputs; original `#if` branches remain in the counterfactual. This is estimated pruning potential, not a production change. |
| 2 | Formatting after source-position checks | 365,214 | Exact lexer-preserving indentation/blank-line counterfactual across 109 already-pruned marker-free outputs. Removes 221,286 indentation bytes and 143,928 blank-line bytes. |
| 3 | Per-declaration rule-1 pruning | 113,534 | Direct owned bytes with evidence (3), restricted to currently pruned artifacts; includes 16,013 explicit-one-argument and 34,246 multi-argument constructor bytes. Guides offer another 22,830 candidate bytes and are separate. |
| 4 | Prune unused free declarations and helper groups | 102,265 | Direct owned bytes in 185 declarations after a lexical dependency fixpoint. The raw no-reference/no-compiler-flag pool is 215,301 bytes; external kept identifier uses shrink it to this conservative pool. |
| 5 | Bound A12 by reached standard facilities | 52,248 | Rendered estimate after full closure; simply dropping scanned-only names saves 19,772 output bytes. Facility and worker-only savings overlap. |

### 1. Certify the new refusal gates

Proposed assumption (not adopted): **The judge’s `__builtin_prefetch` has no implicit user-declaration dependencies beyond the argument expressions that are traversed; every alternative of an unresolved floating-evaluation conditional has no lookup, declaration or instantiation dependency outside the union retained for those alternatives. Prefer proving this for the bounded constructs over introducing a blanket assumption.**

Do not replace judge `FLT_EVAL_METHOD` with the worker value: x87/excess precision can differ. In `geometry/predicates.hpp:38`, the nonzero branch contains a volatile binary64 store that must survive. `aho-corasick/quick` exercises the builtin gate. Both program branches must remain semantically available.

### 2. Formatting after source-position checks

Proposed assumption (not adopted): **The program’s observable behaviour and compilation success do not depend on horizontal indentation or redundant empty physical lines outside literals, comments, directives and line continuations, once the existing source-position observation gate has passed. This strengthens no library-dispatch assumption.**

Preserve multiline literals, directive logical lines, continuations and token/directive boundaries. `fast-io/roundtrip` remains ineligible while it falls back with position-sensitive macro evidence. A token-equivalence check alone cannot replace the position gate.

### 3. Per-declaration rule-1 pruning

Proposed assumption (not adopted): **The judge’s user and library overload resolution, implicit conversions, constraints, traits, deduction and virtual dispatch do not name or require a rule-1 declaration that has no reference or relevant trait/unevaluated evidence in the kept worker program. Explicit constructors need direct-initialization and trait coverage too.**

ATL `matrix` has range and dimension constructors; `persistent_ring_backing` tests `constructible_from<Backing, size_t>`, and `top_tree` constrains delegating constructors. Target-selected size/alias types can select another overload or change a constraint without changing user tokens. This needs declaration-specific proofs or a strong new assumption. Splitting overload units, constructor-name/type-name tokens, A12 constructor protocols, the static-name invariant and reverse effects must be redesigned together; merely switching off rule 1 saves nothing for constructors still protected by coarse same-name rules.

### 4. Prune unused free declarations and helper groups

Proposed assumption (not adopted): **The judge does not select a worker-unreferenced free-function or hidden-friend overload through ADL, operators, constraints or a library-only instantiation absent from the kept worker program, and discarded helper groups have no remaining kept lexical or potential-effect users. This narrows A6 and the unconditional free-operator protection.**

ATL `mod_int` keeps hidden stream operators and their `uint128_stream` helpers; a different library/target can instantiate those dormant protocols. The raw pool also contains unused `convolution_uint64` overloads retained with referenced overloads. Keep helper-name lookup valid even inside uninstantiated retained templates, and preserve reverse potential effects. A lack of a resolved call alone is insufficient.

### 5. Bound A12 by reached standard facilities

Proposed assumption (not adopted): **The judge’s standard library can name a user member only through an identifier in the macro-expanded or raw declarations of the standard facilities transitively reachable from the facilities used by this TU on the worker, or through an independently protected language/operator/conversion/constructor/destructor protocol. A whole-library scanned name alone is not an additional lookup source.**

A different library build can choose a dormant implementation path or member protocol that the worker never instantiated. ATL retained range/container APIs (`data`, `size`, `reserve`, `max_size`) and allocator-backed structures are concrete surfaces for such divergence. The recorded facility closure is a broad worker estimate, not an all-implementation reachability proof.

The all-126 rule-1 evidence-(3) count includes declarations inside flat artifacts. Those remain independently protected by global refusal and are excluded from the actionable rule-1 ranked pool. Constructor explicitness is useful for narrowing the audit, but does not make an observed-unused overload portable to remove.

Name-collision refinement remains a smaller additional opportunity: 9,392 owned bytes in R-nondep-other + R-dep(a) + R-cycle. The 1,987-byte `coordinate_compression::ranks` collision in each coordinate-compression example is a concrete case, subject to the prior audit’s target-stable-binding requirement.

## Verification and reproducibility

Pinned LLVM 23.1.5 Linux ARM64 SDK SHA-256: `5ca712f18c710592a8fa89758246a6b6cbdbd16c1fbd5f12c7f5bcdf35fe06de`, verified against release SHA256SUMS. SDK/build are ignored. Configure/build hard caps: 120/900 seconds; rebuilds 600 seconds; `-j2`, Clang 22.1.8, `-O1 -DNDEBUG`, LLD. All six focused CTest suites pass (`unit`, `embedded_members`, `prune_extract_cases`, `prune_extract_models`, `member_pruning`, `member_review`) after the completed diagnostic rebuild.

Generation cap 240 seconds/example, 2,500,000 KiB address-space cap, two examples concurrently. Standalone compile/run caps 120/30 seconds. Completed sample checks represented here: 126/126. Cross GCC 15.2 and QEMU 10.1 packages are checksum-verified against public Ubuntu indexes and extracted entirely inside this worktree.

The new geometry fallbacks retain unresolved `FLT_EVAL_METHOD != 0` in `geometry/predicates.hpp`; Aho–Corasick encounters unsupported `__builtin_prefetch`. Those outputs remain in the whole-output denominator; their ordinary units are R-other under global refusal. No refusal was bypassed for the measured artifacts. The separate gate-waiver experiments retain the source conditional branches and run the production closure after clearing only the bounded refusal reasons; their gains are speculative until proper branch/dependency support is designed.

Raw models, declarations, references, rendered copy maps, sample outputs and reports are under ignored `measurements/trim/`; tooling and build/test/acquisition logs are under `.checks/trim/`. Diagnostic patch: `docs/reviews/trim-analysis-diag.patch` (saved before production sources are restored). SDK deleted after build/test; all 126 artifacts and classifications reconcile. Baseline-worker model equality after sorting/deduplicating unordered graph edges: 126/126 saved models. Production sources are restored after saving the diagnostic patch; only this review and that patch are intended tracked changes.

## Free functions and hidden-friend candidates

The raw declaration-level pool with no kept resolved reference and no compiler used/referenced flag is 215,301 owned bytes. A lexical fixpoint over kept identifier tokens retains a candidate whenever its name still appears outside the proposed deletion group, including in a kept uninstantiated function body. This leaves 185 declarations / 102,265 bytes. Only currently pruned artifacts are included; roots/pins and `main` are excluded. This is a candidate-group estimate requiring the proposed A6/dispatch/effect conditions, not a new production deletion rule.

| Source declaration | Candidate owned bytes |
| --- | ---: |
| `atl/nt/detail/uint128_stream.hpp:105` `atl::detail::read_stream_magnitude` | 41,225 |
| `atl/nt/basic.hpp:137` `atl::factorization` | 12,396 |
| `atl/utils/counting_sort.hpp:108` `atl::detail::counting_permutation` | 8,568 |
| `atl/nt/mod_int.hpp:165` `atl::operator>>` | 7,452 |
| `atl/nt/mod_int.hpp:148` `atl::operator<<` | 5,831 |
| `atl/ds/static_vector.hpp:230` `atl::operator<=>` | 3,740 |
| `atl/graph/traversal.hpp:1007` `atl::graph::dfs_fold` | 3,426 |
| `atl/ds/static_vector.hpp:223` `atl::operator==` | 2,788 |
| `atl/nt/sieve.hpp:93` `atl::sieve_info` | 1,995 |
| `atl/nt/matrix.hpp:185` `atl::detail::lazy_product` | 1,801 |
| `atl/graph/bipartite_matching.hpp:584` `atl::graph::matching_edge_roles` | 1,795 |
| `atl/nt/matrix.hpp:361` `atl::operator*` | 1,074 |
| `atl/nt/matrix.hpp:395` `atl::operator*` | 1,036 |
| `atl/graph/bipartite_matching.hpp:295` `atl::graph::minimum_vertex_cover` | 907 |
| `atl/geometry/point.hpp:365` `atl::geometry::detail::float64_difference` | 772 |

## Supplemental validation

Baseline replay preserves family identities, byte ownership, references, dependent-name sets, inventories and graph connectivity. Raw serialized edge order differs because the diagnostic traversal changes redundant visitation order; sorting/deduplicating unordered graph-edge lists restores equality. No retention decision is changed by that normalization.

All 16 successfully materialized gate-waiver outputs passed standalone compilation and sample output: 16/16. This validates the local counterfactual only. The saved counterfactuals occupy 992,840 bytes versus 1,398,006 original flat bytes.

| Flat example | Original bytes | Gate-waiver bytes | Estimated gain | Sample |
| --- | ---: | ---: | ---: | --- |
| aho-corasick-quick | 49,157 | 23,556 | 25,601 | pass |
| arrangement-quick | 102,172 | 76,951 | 25,221 | pass |
| calipers-quick | 90,936 | 60,194 | 30,742 | pass |
| circle-quick | 100,041 | 71,725 | 28,316 | pass |
| convex-hull-quick | 70,799 | 38,891 | 31,908 | pass |
| convex-polygon-operations-quick | 130,778 | 94,496 | 36,282 | pass |
| delaunay-quick | 190,738 | 141,947 | 48,791 | pass |
| geometry-primitives-boxes-and-angles | 42,180 | 27,498 | 14,682 | pass |
| geometry-primitives-quick | 34,351 | 20,463 | 13,888 | pass |
| geometry-primitives-segments-and-lines | 49,373 | 34,239 | 15,134 | pass |
| halfplane-intersection-quick | 96,791 | 74,148 | 22,643 | pass |
| point-location-quick | 128,135 | 98,182 | 29,953 | pass |
| polygon-overlay-quick | 108,482 | 84,440 | 24,042 | pass |
| polygon-quick | 48,527 | 32,794 | 15,733 | pass |
| segment-intersections-quick | 56,362 | 37,042 | 19,320 | pass |
| triangulation-quick | 99,184 | 76,274 | 22,910 | pass |

The constructor-overload, constructibility-trait and member-alias LP64/ILP32 checks pass all 12/12 expected full/deleted syntax-check outcomes. Each worker-unused candidate becomes necessary on i386; these constructed checks illustrate the mechanism, while the real ATL constraint and alias sites are identified in the recommendations.

Additional compacted/candidate artifacts checked: 18/18; details are in the local `supplement.json` records. All compaction measurements preserve the complete lexical-token sequence and directive boundaries. The free-declaration diagnostic was corrected to exclude lambda initializer bodies and to remove full declaration-owned ranges including leading attributes; the final group checks use that corrected extractor. The main sweep used two examples concurrently. Three early supplemental gate checks briefly overlapped that sweep before scheduling was corrected; remaining example checks were deferred until the sweep finished.

## Appendix: complete top-15 category breakdown

Each cell is **output bytes / no-positive-AST bytes**. T1–T15 follow the order in the top-15 table above.

| Category | T1 | T2 | T3 | T4 | T5 | T6 | T7 | T8 | T9 | T10 | T11 | T12 | T13 | T14 | T15 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Example's own code | 364 / 364 | 477 / 477 | 552 / 552 | 901 / 901 | 674 / 674 | 464 / 464 | 935 / 935 | 356 / 356 | 1,026 / 1,026 | 416 / 416 | 933 / 933 | 511 / 511 | 732 / 732 | 492 / 492 | 652 / 652 |
| Preprocessor directives and includes | 3,478 / 3,478 | 2,292 / 2,292 | 3,334 / 3,334 | 4,608 / 4,608 | 2,626 / 2,626 | 2,921 / 2,921 | 4,180 / 4,180 | 2,789 / 2,789 | 3,794 / 3,794 | 2,353 / 2,353 | 4,348 / 4,348 | 2,510 / 2,510 | 4,367 / 4,367 | 1,995 / 1,995 | 1,802 / 1,802 |
| Namespace and record scaffolding | 9,357 / 1,231 | 6,873 / 871 | 9,863 / 1,008 | 7,096 / 1,027 | 12,381 / 691 | 8,862 / 956 | 7,405 / 957 | 8,716 / 905 | 6,998 / 913 | 6,933 / 814 | 7,186 / 970 | 7,330 / 851 | 7,276 / 970 | 5,887 / 738 | 6,389 / 707 |
| Data members | 2,158 / 422 | 1,502 / 168 | 2,256 / 264 | 1,725 / 101 | 3,246 / 269 | 1,839 / 249 | 1,952 / 135 | 1,711 / 264 | 1,615 / 101 | 550 / 160 | 1,795 / 130 | 627 / 138 | 1,867 / 211 | 609 / 170 | 1,322 / 168 |
| Member functions: non-rule-1 | 16,733 / 5,594 | 7,744 / 2,604 | 34,219 / 13,952 | 13,996 / 3,350 | 50,367 / 8,089 | 20,736 / 8,486 | 21,889 / 3,911 | 20,736 / 8,039 | 12,194 / 3,279 | 5,048 / 3,546 | 12,194 / 3,683 | 13,209 / 7,366 | 12,194 / 3,683 | 7,298 / 5,013 | 6,837 / 2,451 |
| ctor-explicit | 993 / 827 | 610 / 610 | 1,935 / 494 | 700 / 217 | 1,062 / 849 | 1,840 / 679 | 1,393 / 217 | 1,840 / 494 | 612 / 217 | 217 / 217 | 612 / 217 | 317 / 317 | 612 / 217 | 89 / 0 | 610 / 0 |
| ctor-implicit-converting | 109 / 109 | 0 / 0 | 0 / 0 | 220 / 109 | 75 / 0 | 0 / 0 | 220 / 109 | 0 / 0 | 220 / 109 | 109 / 109 | 220 / 109 | 0 / 0 | 220 / 109 | 0 / 0 | 0 / 0 |
| multi-arg ctor | 3,897 / 3,772 | 3,772 / 3,202 | 3,787 / 89 | 0 / 0 | 6,830 / 3,613 | 79 / 0 | 0 / 0 | 79 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 79 / 79 | 0 / 0 | 0 / 0 | 3,772 / 3,027 |
| default ctor | 189 / 78 | 137 / 62 | 262 / 97 | 803 / 96 | 413 / 66 | 127 / 58 | 803 / 96 | 127 / 58 | 803 / 96 | 36 / 0 | 803 / 96 | 106 / 37 | 803 / 96 | 0 / 0 | 137 / 62 |
| copy/move ctor | 0 / 0 | 0 / 0 | 46 / 46 | 0 / 0 | 0 / 0 | 46 / 46 | 0 / 0 | 46 / 46 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| destructor | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| copy/move assignment | 0 / 0 | 0 / 0 | 57 / 57 | 0 / 0 | 0 / 0 | 57 / 57 | 0 / 0 | 57 / 57 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| operator() | 3,521 / 3,361 | 0 / 0 | 5,211 / 3,475 | 0 / 0 | 7,450 / 4,028 | 4,902 / 3,166 | 0 / 0 | 4,902 / 3,166 | 0 / 0 | 3,521 / 3,361 | 0 / 0 | 4,703 / 3,293 | 0 / 0 | 398 / 398 | 0 / 0 |
| operator-comparison | 0 / 0 | 0 / 0 | 79 / 0 | 248 / 159 | 1,022 / 944 | 79 / 0 | 248 / 159 | 79 / 0 | 248 / 159 | 0 / 0 | 248 / 159 | 79 / 79 | 248 / 159 | 0 / 0 | 0 / 0 |
| operator-arithmetic | 2,242 / 1,275 | 2,242 / 776 | 1,531 / 905 | 963 / 434 | 257 / 257 | 1,531 / 905 | 963 / 434 | 1,531 / 905 | 963 / 434 | 301 / 198 | 963 / 434 | 558 / 558 | 963 / 434 | 301 / 198 | 1,481 / 386 |
| operator-stream | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| operator-other | 337 / 337 | 121 / 121 | 572 / 191 | 216 / 216 | 440 / 440 | 440 / 191 | 216 / 216 | 440 / 191 | 216 / 216 | 216 / 102 | 216 / 216 | 440 / 64 | 216 / 216 | 106 / 0 | 121 / 121 |
| conversion-explicit | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| conversion-implicit | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| deduction guide | 522 / 522 | 522 / 522 | 303 / 303 | 0 / 0 | 7,924 / 7,924 | 182 / 182 | 0 / 0 | 182 / 182 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 522 / 522 |
| virtual | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| Free functions and function templates | 119,315 / 83,080 | 84,119 / 30,786 | 32,985 / 11,180 | 79,761 / 10,386 | 5,282 / 707 | 35,555 / 9,836 | 53,116 / 8,670 | 30,889 / 11,087 | 60,339 / 8,670 | 59,617 / 22,163 | 58,062 / 9,114 | 43,031 / 15,384 | 56,704 / 9,114 | 58,633 / 27,332 | 50,273 / 23,416 |
| Type aliases and using-declarations | 3,474 / 1,464 | 2,759 / 1,162 | 3,757 / 1,236 | 2,417 / 909 | 4,617 / 959 | 3,623 / 1,257 | 2,417 / 909 | 3,623 / 1,257 | 2,417 / 909 | 2,819 / 1,304 | 2,533 / 909 | 3,220 / 1,071 | 2,533 / 909 | 2,599 / 985 | 2,733 / 1,136 |
| Variables and constants | 2,951 / 2,162 | 1,552 / 1,083 | 2,755 / 1,977 | 1,525 / 584 | 2,524 / 1,619 | 2,755 / 2,081 | 1,250 / 584 | 2,755 / 2,081 | 1,189 / 584 | 2,501 / 1,977 | 1,252 / 584 | 2,755 / 2,081 | 1,252 / 584 | 1,756 / 794 | 1,552 / 1,083 |
| static_asserts | 0 / 0 | 0 / 0 | 0 / 0 | 499 / 499 | 0 / 0 | 0 / 0 | 341 / 341 | 0 / 0 | 341 / 341 | 0 / 0 | 341 / 341 | 0 / 0 | 341 / 341 | 0 / 0 | 0 / 0 |
| Concepts and requires clauses | 5,530 / 2,038 | 4,302 / 1,217 | 10,489 / 2,490 | 6,182 / 526 | 15,254 / 2,480 | 9,602 / 2,192 | 5,676 / 526 | 9,150 / 2,080 | 6,199 / 526 | 3,808 / 608 | 5,925 / 526 | 8,410 / 1,906 | 5,524 / 526 | 6,026 / 799 | 3,939 / 901 |
| Enums | 634 / 0 | 418 / 0 | 261 / 0 | 109 / 0 | 166 / 0 | 96 / 0 | 0 / 0 | 96 / 0 | 0 / 0 | 634 / 0 | 118 / 0 | 311 / 0 | 118 / 0 | 516 / 0 | 215 / 0 |
| Other | 14,934 / 14,934 | 11,336 / 11,336 | 13,881 / 13,881 | 2,280 / 2,280 | 1,559 / 1,559 | 12,746 / 12,746 | 2,051 / 2,051 | 12,068 / 12,068 | 1,860 / 1,860 | 10,962 / 10,962 | 2,094 / 2,094 | 10,988 / 10,988 | 2,094 / 2,094 | 10,086 / 10,086 | 8,579 / 8,579 |
| **Total** | 190,738 / 125,048 | 130,778 / 57,289 | 128,135 / 55,531 | 124,249 / 26,402 | 124,169 / 37,794 | 108,482 / 46,472 | 105,055 / 24,430 | 102,172 / 46,025 | 101,034 / 23,234 | 100,041 / 48,290 | 99,843 / 24,863 | 99,184 / 47,233 | 98,064 / 24,762 | 96,791 / 49,000 | 90,936 / 45,013 |

## Appendix: per-example member retention

Cells are units / owned bytes, using the same exclusive classes as the totals. Rule-1 member units are separate.

| Example | R-AST | R-lib | R-dep(a) | R-dep(b) | R-dep(c) | R-nondep-other | R-macro | R-cycle | R-other | rule1 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| aggregate-queue-deque-sliding-window | 24 / 4,378 | 12 / 1,573 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 1,222 |
| aho-corasick-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 54 / 11,439 | 18 / 7,491 |
| arrangement-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 76 / 22,833 | 35 / 9,260 |
| assignment-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| bipartite-matching-dag | 14 / 3,743 | 1 / 139 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 2,399 |
| bipartite-matching-quick | 12 / 3,030 | 1 / 139 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 2,399 |
| bit-utilities-combinations | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| bitwise-convolution-quick | 20 / 3,190 | 4 / 709 | 0 / 0 | 0 / 0 | 8 / 722 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 16 / 1,887 |
| calipers-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 30 / 7,066 | 15 / 6,367 |
| centroid-decomposition-quick | 4 / 942 | 5 / 636 | 0 / 0 | 0 / 0 | 4 / 1,862 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 1,971 |
| circle-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 26 / 5,270 | 16 / 4,526 |
| closest-pair-quick | 1 / 231 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 301 |
| convex-hull-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 13 / 3,368 | 5 / 460 |
| convex-piecewise-linear-forest-quick | 40 / 10,440 | 12 / 2,170 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 103 | 0 / 0 | 0 / 0 | 2 / 175 | 15 / 3,061 |
| convex-piecewise-linear-moving | 23 / 8,467 | 3 / 412 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 5 / 982 |
| convex-piecewise-linear-quick | 18 / 5,484 | 3 / 481 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 5 / 982 |
| convex-polygon-operations-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 32 / 7,973 | 18 / 7,128 |
| convolution-quick | 36 / 8,528 | 18 / 2,795 | 1 / 71 | 0 / 0 | 11 / 947 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 18 / 3,188 |
| coordinate-compression-basic | 2 / 384 | 3 / 348 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 1,987 | 0 / 0 | 0 / 0 | 2 / 126 | 3 / 1,445 |
| coordinate-compression-move-only | 2 / 384 | 3 / 348 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 1,987 | 0 / 0 | 0 / 0 | 2 / 126 | 5 / 1,586 |
| counting-sort-stable-records | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| crt-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 76 |
| delaunay-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 68 / 17,006 | 33 / 11,660 |
| difference-fenwick-basic | 14 / 5,224 | 2 / 176 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 2,357 |
| discrete-log-quick | 44 / 9,808 | 37 / 7,896 | 0 / 0 | 0 / 0 | 3 / 207 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 464 | 30 / 7,125 |
| dp-optimization-interval-merge | 2 / 294 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 275 |
| dp-optimization-partition | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 634 |
| dp-optimization-quick | 1 / 314 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 275 |
| dp-optimization-row-minima | 2 / 1,812 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 154 |
| dsu-history | 29 / 9,022 | 10 / 990 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 194 | 0 / 0 | 0 / 0 | 0 / 0 | 12 / 2,288 |
| dual-segment-tree-quick-start | 9 / 2,651 | 3 / 343 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 1,902 |
| dynamic-lazy-segment-tree-quick-start | 12 / 4,978 | 4 / 569 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 969 |
| dynamic-segment-tree-quick-start | 5 / 2,618 | 4 / 878 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 428 |
| euler-tour-tree-basic | 68 / 21,203 | 19 / 5,720 | 0 / 0 | 0 / 0 | 3 / 1,133 | 1 / 206 | 0 / 0 | 0 / 0 | 4 / 508 | 28 / 7,878 |
| exact-count-optimum-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 634 |
| factorization-quick | 12 / 1,885 | 20 / 3,620 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 149 | 0 / 0 | 0 / 0 | 2 / 216 | 6 / 1,290 |
| fast-io-roundtrip | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 71 / 16,243 | 21 / 2,590 |
| fenwick-tree-basic | 9 / 3,928 | 2 / 209 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 1,722 |
| flat-hash-basic | 15 / 5,216 | 29 / 7,803 | 0 / 0 | 0 / 0 | 1 / 160 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 634 | 22 / 5,618 |
| floor-sum-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 80 |
| functional-graphs-quick | 28 / 13,035 | 10 / 1,157 | 1 / 128 | 0 / 0 | 2 / 1,090 | 1 / 124 | 0 / 0 | 0 / 0 | 0 / 0 | 15 / 7,386 |
| gcd-bezout | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| geometry-primitives-boxes-and-angles | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 12 / 3,298 | 4 / 655 |
| geometry-primitives-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 12 / 3,298 | 3 / 301 |
| geometry-primitives-segments-and-lines | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 12 / 3,298 | 3 / 301 |
| graph-callbacks | 9 / 1,420 | 1 / 64 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 1,018 |
| graph-components-blocks | 16 / 4,562 | 1 / 139 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 863 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 2,399 |
| graph-components-bridges | 14 / 4,022 | 1 / 139 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 2,399 |
| graph-components-scc | 12 / 3,249 | 1 / 139 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 2,399 |
| graph-quick | 15 / 3,930 | 3 / 269 | 0 / 0 | 0 / 0 | 1 / 205 | 1 / 391 | 0 / 0 | 0 / 0 | 0 / 0 | 11 / 4,832 |
| halfplane-intersection-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 26 / 7,573 | 6 / 894 |
| hierarchical-bitset-basic | 6 / 1,727 | 5 / 1,031 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 69 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 899 |
| int128-arithmetic | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| lazy-segment-tree-quick-start | 13 / 3,863 | 3 / 294 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 1,545 |
| lca-path-sum | 13 / 7,931 | 10 / 1,189 | 0 / 0 | 0 / 0 | 2 / 1,090 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 2,464 |
| lca-quick | 7 / 5,738 | 5 / 1,535 | 0 / 0 | 0 / 0 | 2 / 1,090 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 1,628 |
| lexicographic-order-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| li-chao-tree-basic | 18 / 11,100 | 4 / 296 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 998 |
| linear-recurrence-quick | 41 / 9,129 | 23 / 3,483 | 1 / 71 | 0 / 0 | 16 / 1,514 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 22 / 4,551 |
| link-cut-tree-basic | 78 / 24,906 | 31 / 9,378 | 0 / 0 | 0 / 0 | 3 / 880 | 1 / 144 | 0 / 0 | 0 / 0 | 4 / 508 | 28 / 11,737 |
| lyndon-quick | 9 / 2,903 | 5 / 350 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 3,500 |
| manhattan-mst-quick | 2 / 691 | 3 / 251 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 483 |
| matching-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| matrix-quick | 35 / 5,218 | 11 / 1,893 | 0 / 0 | 0 / 0 | 11 / 958 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 16 / 2,997 |
| max-flow-bounds | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 1,173 |
| max-flow-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 1,173 |
| meldable-heap-forest-basic | 18 / 4,885 | 6 / 901 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 75 | 6 / 1,446 |
| min-cost-flow-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 2,489 |
| min-cost-flow-supplies | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 1,173 |
| modular-arithmetic-residues | 30 / 4,502 | 9 / 1,451 | 1 / 71 | 0 / 0 | 10 / 846 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 665 | 23 / 3,313 |
| multiplicative-functions-quick | 1 / 122 | 11 / 1,634 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 4 / 863 |
| online-convolution-partitions | 41 / 13,550 | 22 / 3,402 | 1 / 71 | 0 / 0 | 10 / 866 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 20 / 4,445 |
| online-convolution-quick | 40 / 13,493 | 22 / 3,402 | 1 / 71 | 0 / 0 | 11 / 947 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 19 / 4,256 |
| palindrome-lengths-quick | 4 / 511 | 1 / 56 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 947 |
| palindromic-tree-quick | 16 / 4,102 | 9 / 1,181 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 300 |
| persistent-array-versions | 24 / 9,623 | 6 / 547 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 11 / 2,406 |
| persistent-heap-basic | 21 / 6,136 | 15 / 2,251 | 0 / 0 | 0 / 0 | 3 / 773 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 1,323 |
| persistent-lazy-segment-tree-basic | 14 / 4,437 | 6 / 2,112 | 0 / 0 | 0 / 0 | 4 / 1,670 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 3,514 |
| persistent-list-basic | 16 / 3,819 | 6 / 769 | 0 / 0 | 0 / 0 | 1 / 396 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 158 | 9 / 2,232 |
| persistent-queue-deque-basic | 37 / 10,682 | 23 / 3,654 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 158 | 19 / 4,209 |
| persistent-segment-tree-basic | 13 / 3,732 | 4 / 941 | 0 / 0 | 0 / 0 | 2 / 736 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 1,745 |
| persistent-treap-sequence-basic | 38 / 9,930 | 13 / 4,625 | 0 / 0 | 0 / 0 | 10 / 7,045 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 3,528 |
| persistent-vector-basic | 67 / 21,270 | 21 / 5,744 | 0 / 0 | 0 / 0 | 4 / 2,861 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 173 | 11 / 2,231 |
| point-location-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 118 / 36,702 | 41 / 13,864 |
| polygon-overlay-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 76 / 22,833 | 35 / 9,260 |
| polygon-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 12 / 3,298 | 3 / 301 |
| polynomial-gcd-pade | 39 / 8,830 | 17 / 2,676 | 2 / 173 | 0 / 0 | 8 / 662 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 18 / 3,188 |
| polynomial-gcd-quick | 39 / 8,830 | 17 / 2,676 | 2 / 173 | 0 / 0 | 8 / 662 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 18 / 3,188 |
| polynomial-points-quick | 51 / 18,429 | 17 / 2,676 | 2 / 173 | 0 / 0 | 9 / 735 | 1 / 155 | 0 / 0 | 0 / 0 | 2 / 216 | 21 / 4,033 |
| polynomial-points-sampling | 44 / 10,660 | 15 / 2,343 | 2 / 173 | 0 / 0 | 8 / 662 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 19 / 3,276 |
| power-monoid | 15 / 2,335 | 8 / 1,483 | 0 / 0 | 0 / 0 | 8 / 760 | 1 / 43 | 0 / 0 | 0 / 0 | 0 / 0 | 11 / 1,356 |
| power-series-powers-and-roots | 42 / 9,234 | 15 / 2,343 | 1 / 102 | 0 / 0 | 8 / 662 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 18 / 3,188 |
| power-series-prime-field-helpers | 29 / 5,326 | 18 / 2,795 | 1 / 102 | 0 / 0 | 7 / 561 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 17 / 2,578 |
| power-series-quick | 39 / 8,830 | 17 / 2,676 | 2 / 173 | 0 / 0 | 8 / 662 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 18 / 3,188 |
| radial-sweep-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 301 |
| rmq-basic | 5 / 1,440 | 2 / 131 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 1,799 |
| segment-intersections-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 33 / 10,720 | 13 / 3,661 |
| segment-tree-beats-quick-start | 27 / 8,118 | 13 / 1,204 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 11 / 3,128 |
| segment-tree-quick-start | 9 / 3,062 | 3 / 227 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 836 |
| series-composition-quick | 46 / 10,965 | 15 / 2,343 | 2 / 173 | 0 / 0 | 8 / 662 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 216 | 19 / 3,276 |
| sieve-quick | 9 / 1,759 | 10 / 1,416 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 71 | 0 / 0 | 0 / 0 | 0 / 0 | 5 / 1,023 |
| sorted-dictionary-quick | 9 / 5,172 | 3 / 183 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 3,770 |
| sparse-table-basic | 2 / 559 | 4 / 194 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 2,279 |
| splay-engine-quick-start | 16 / 5,690 | 5 / 1,084 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 144 | 1 / 29 |
| splay-map-quick-start | 34 / 11,614 | 19 / 7,982 | 0 / 0 | 0 / 0 | 5 / 3,186 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 144 | 9 / 2,925 |
| splay-sequence-quick-start | 39 / 16,368 | 20 / 4,658 | 0 / 0 | 0 / 0 | 2 / 663 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 144 | 8 / 4,786 |
| static-trie-quick | 6 / 1,611 | 5 / 734 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 4,765 |
| static-vector-basic | 7 / 1,271 | 7 / 701 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 704 |
| string-hash-quick | 14 / 3,469 | 4 / 607 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 43 | 2 / 810 |
| subtree-link-cut-tree-basic | 96 / 31,085 | 24 / 7,871 | 1 / 308 | 0 / 0 | 3 / 1,400 | 1 / 144 | 0 / 0 | 0 / 0 | 4 / 508 | 29 / 12,466 |
| suffix-array-quick | 3 / 1,355 | 4 / 329 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 1,701 |
| suffix-automaton-quick | 18 / 6,579 | 9 / 1,140 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 196 |
| suffix-tree-quick | 22 / 8,521 | 12 / 1,751 | 1 / 124 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 220 |
| time-divide-and-conquer-connectivity | 27 / 6,412 | 23 / 6,232 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 248 | 26 / 5,720 |
| top-tree-basic | 140 / 44,792 | 31 / 8,322 | 1 / 77 | 0 / 0 | 1 / 160 | 0 / 0 | 0 / 0 | 0 / 0 | 4 / 508 | 62 / 19,914 |
| treap-map-quick-start | 35 / 10,207 | 16 / 5,301 | 0 / 0 | 0 / 0 | 4 / 2,778 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 7 / 2,895 |
| treap-sequence-quick-start | 31 / 9,748 | 19 / 5,400 | 0 / 0 | 0 / 0 | 4 / 1,543 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 6 / 4,815 |
| triangulation-quick | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 53 / 15,250 | 25 / 6,354 |
| trie-quick | 13 / 3,880 | 12 / 1,556 | 1 / 127 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 174 |
| types-and-utilities-composite-key | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 10 / 3,380 |
| virtual-tree-quick | 7 / 5,226 | 4 / 1,526 | 0 / 0 | 0 / 0 | 2 / 1,090 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 1,628 |
| virtual-tree-separation | 8 / 5,340 | 3 / 1,412 | 0 / 0 | 0 / 0 | 2 / 1,090 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 1,628 |
| wavelet-matrix-basic | 12 / 3,100 | 6 / 722 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 1,630 |
| weighted-dsu-matrix | 26 / 4,756 | 10 / 1,393 | 1 / 102 | 0 / 0 | 6 / 556 | 1 / 43 | 0 / 0 | 0 / 0 | 0 / 0 | 17 / 3,280 |
| weighted-dsu-potentials | 37 / 7,323 | 14 / 1,972 | 1 / 102 | 0 / 0 | 6 / 556 | 1 / 43 | 0 / 0 | 0 / 0 | 0 / 0 | 15 / 4,666 |
| xor-basis-queries | 8 / 1,950 | 10 / 1,219 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 111 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 280 |

## Appendix: per-declaration rule-1 evidence

Written source declarations are grouped by location, kind and evidence class; occurrences sum their appearance in different example outputs. A source declaration can have multiple evidence rows when examples provide different witnesses. This table includes every counted declaration and reconciles to the kind/evidence totals above.

| Written source declaration | Kind | Evidence | Occurrences | Owned bytes |
| --- | --- | ---: | ---: | ---: |
| `/tmp/claude-1000/-workspace-cpp-distill/f0c845e5-23d4-411f-9b94-324515809aa1/scratchpad/atl/docs/examples/coordinate-compression/move-only.cpp:10` `ordering::ordering` | copy/move ctor | 3 | 1 | 36 |
| `/tmp/claude-1000/-workspace-cpp-distill/f0c845e5-23d4-411f-9b94-324515809aa1/scratchpad/atl/docs/examples/coordinate-compression/move-only.cpp:12` `ordering::operator()` | operator() | 1 | 1 | 53 |
| `/tmp/claude-1000/-workspace-cpp-distill/f0c845e5-23d4-411f-9b94-324515809aa1/scratchpad/atl/docs/examples/coordinate-compression/move-only.cpp:8` `ordering::ordering` | default ctor | 1 | 1 | 21 |
| `/tmp/claude-1000/-workspace-cpp-distill/f0c845e5-23d4-411f-9b94-324515809aa1/scratchpad/atl/docs/examples/coordinate-compression/move-only.cpp:9` `ordering::ordering` | copy/move ctor | 1 | 1 | 31 |
| `/tmp/claude-1000/-workspace-cpp-distill/f0c845e5-23d4-411f-9b94-324515809aa1/scratchpad/atl/docs/examples/euler-tour-tree/basic.cpp:11` `add_sums::operator()` | operator() | 1 | 1 | 99 |
| `/tmp/claude-1000/-workspace-cpp-distill/f0c845e5-23d4-411f-9b94-324515809aa1/scratchpad/atl/docs/examples/euler-tour-tree/basic.cpp:15` `add_to_each::operator()` | operator() | 1 | 1 | 95 |
| `/tmp/claude-1000/-workspace-cpp-distill/f0c845e5-23d4-411f-9b94-324515809aa1/scratchpad/atl/docs/examples/weighted-dsu/matrix.cpp:10` `matrix::operator==` | operator-comparison | 1 | 1 | 47 |
| `/tmp/claude-1000/-workspace-cpp-distill/f0c845e5-23d4-411f-9b94-324515809aa1/scratchpad/atl/docs/examples/weighted-dsu/matrix.cpp:15` `multiply::operator()` | operator() | 1 | 1 | 165 |
| `/tmp/claude-1000/-workspace-cpp-distill/f0c845e5-23d4-411f-9b94-324515809aa1/scratchpad/atl/docs/examples/weighted-dsu/matrix.cpp:22` `inverse::operator()` | operator() | 1 | 1 | 75 |
| `atl/bits/xor_basis.hpp:132` `atl::xor_basis::xor_basis<Word>` | default ctor | 3 | 1 | 41 |
| `atl/bits/xor_basis.hpp:141` `atl::xor_basis::xor_basis<Word>` | ctor-explicit | 1 | 1 | 129 |
| `atl/bits/xor_basis.hpp:324` `atl::xor_basis::operator==` | operator-comparison | 3 | 1 | 70 |
| `atl/bits/xor_basis.hpp:333` `atl::<deduction guide for xor_basis>` | deduction guide | 2 | 1 | 158 |
| `atl/bits/xor_basis.hpp:377` `atl::stamped_xor_basis::stamped_xor_basis<Word, Stamp>` | default ctor | 1 | 1 | 40 |
| `atl/dp/convex_piecewise_linear.hpp:105` `atl::detail::breakpoint_left_of::operator()` | operator() | 1 | 3 | 528 |
| `atl/dp/convex_piecewise_linear.hpp:113` `atl::detail::breakpoint_right_of::operator()` | operator() | 1 | 3 | 528 |
| `atl/dp/convex_piecewise_linear.hpp:236` `atl::convex_piecewise_linear::convex_piecewise_linear<Value, Slope>` | default ctor | 1 | 2 | 92 |
| `atl/dp/convex_piecewise_linear.hpp:527` `atl::convex_piecewise_linear::operator()` | operator() | 3 | 2 | 976 |
| `atl/dp/convex_piecewise_linear.hpp:554` `atl::convex_piecewise_linear::by_position::operator()` | operator() | 1 | 1 | 96 |
| `atl/dp/convex_piecewise_linear.hpp:554` `atl::convex_piecewise_linear::by_position::operator()` | operator() | 3 | 1 | 96 |
| `atl/dp/convex_piecewise_linear_forest.hpp:103` `atl::detail::breakpoint_pool::breakpoint_pool<Position, Slope>` | ctor-explicit | 1 | 1 | 122 |
| `atl/dp/convex_piecewise_linear_forest.hpp:389` `atl::convex_piecewise_linear_forest::function_id::function_id` | ctor-explicit | 1 | 1 | 72 |
| `atl/dp/convex_piecewise_linear_forest.hpp:397` `atl::convex_piecewise_linear_forest::function_id::function_id` | default ctor | 2 | 1 | 43 |
| `atl/dp/convex_piecewise_linear_forest.hpp:419` `atl::convex_piecewise_linear_forest::function_reference::function_reference` | multi-arg ctor | 1 | 1 | 132 |
| `atl/dp/convex_piecewise_linear_forest.hpp:46` `atl::detail::translate_breakpoint::operator()` | operator() | 1 | 1 | 199 |
| `atl/dp/convex_piecewise_linear_forest.hpp:656` `atl::convex_piecewise_linear_forest::const_function_reference::const_function_reference` | multi-arg ctor | 3 | 1 | 144 |
| `atl/dp/convex_piecewise_linear_forest.hpp:677` `atl::convex_piecewise_linear_forest::convex_piecewise_linear_forest<Position, Slope>` | default ctor | 1 | 1 | 207 |
| `atl/dp/convex_piecewise_linear_forest.hpp:713` `atl::convex_piecewise_linear_forest::operator[]` | operator-other | 1 | 1 | 163 |
| `atl/dp/convex_piecewise_linear_forest.hpp:722` `atl::convex_piecewise_linear_forest::operator[]` | operator-other | 3 | 1 | 181 |
| `atl/dp/detail/wide.hpp:104` `atl::detail::total_order::operator()` | operator() | 1 | 4 | 1,100 |
| `atl/dp/optimal_partition.hpp:143` `atl::detail::adaptive_partition_choice::operator()` | operator() | 1 | 2 | 264 |
| `atl/dp/optimal_partition.hpp:44` `atl::detail::knuth_window::knuth_window` | multi-arg ctor | 1 | 2 | 454 |
| `atl/dp/row_minima.hpp:221` `atl::detail::smawk::smawk<Eval, Better>` | multi-arg ctor | 1 | 1 | 154 |
| `atl/ds/aggregate_deque.hpp:101` `atl::aggregate_deque::operator=` | copy/move assignment | 3 | 1 | 67 |
| `atl/ds/aggregate_deque.hpp:81` `atl::aggregate_deque::aggregate_deque<Value, Operation>` | ctor-explicit | 1 | 1 | 155 |
| `atl/ds/aggregate_deque.hpp:84` `atl::aggregate_deque::aggregate_deque<Value, Operation>` | copy/move ctor | 3 | 1 | 61 |
| `atl/ds/aggregate_deque.hpp:87` `atl::aggregate_deque::aggregate_deque<Value, Operation>` | copy/move ctor | 3 | 1 | 56 |
| `atl/ds/aggregate_deque.hpp:90` `atl::aggregate_deque::operator=` | copy/move assignment | 3 | 1 | 232 |
| `atl/ds/aggregate_queue.hpp:49` `atl::aggregate_queue::aggregate_queue<Value, Operation>` | ctor-explicit | 1 | 1 | 155 |
| `atl/ds/aggregate_queue.hpp:52` `atl::aggregate_queue::aggregate_queue<Value, Operation>` | copy/move ctor | 3 | 1 | 61 |
| `atl/ds/aggregate_queue.hpp:55` `atl::aggregate_queue::aggregate_queue<Value, Operation>` | copy/move ctor | 3 | 1 | 56 |
| `atl/ds/aggregate_queue.hpp:58` `atl::aggregate_queue::operator=` | copy/move assignment | 3 | 1 | 232 |
| `atl/ds/aggregate_queue.hpp:69` `atl::aggregate_queue::operator=` | copy/move assignment | 3 | 1 | 67 |
| `atl/ds/binary_indexed_tree.hpp:217` `atl::binary_indexed_tree::binary_indexed_tree<T, Dim, Op>` | ctor-explicit | 1 | 1 | 231 |
| `atl/ds/binary_indexed_tree.hpp:217` `atl::binary_indexed_tree::binary_indexed_tree<T, Dim, Op>` | ctor-explicit | 3 | 1 | 231 |
| `atl/ds/binary_indexed_tree.hpp:226` `atl::binary_indexed_tree::binary_indexed_tree<T, Dim, Op>` | ctor-explicit | 3 | 2 | 404 |
| `atl/ds/binary_indexed_tree.hpp:241` `atl::binary_indexed_tree::binary_indexed_tree<T, Dim, Op>` | ctor-explicit | 1 | 2 | 1,352 |
| `atl/ds/binary_indexed_tree.hpp:258` `atl::binary_indexed_tree::binary_indexed_tree<T, Dim, Op>` | multi-arg ctor | 3 | 2 | 1,066 |
| `atl/ds/binary_indexed_tree.hpp:345` `atl::<deduction guide for binary_indexed_tree>` | deduction guide | 3 | 2 | 248 |
| `atl/ds/binary_indexed_tree.hpp:348` `atl::<deduction guide for binary_indexed_tree>` | deduction guide | 3 | 2 | 184 |
| `atl/ds/binary_indexed_tree.hpp:352` `atl::<deduction guide for binary_indexed_tree>` | deduction guide | 3 | 2 | 320 |
| `atl/ds/binary_indexed_tree.hpp:355` `atl::<deduction guide for binary_indexed_tree>` | deduction guide | 3 | 2 | 256 |
| `atl/ds/binary_indexed_tree.hpp:360` `atl::<deduction guide for binary_indexed_tree>` | deduction guide | 3 | 2 | 526 |
| `atl/ds/coordinate_compression.hpp:163` `atl::coordinate_compression::coordinate_compression<T, Compare>` | default ctor | 3 | 2 | 90 |
| `atl/ds/coordinate_compression.hpp:182` `atl::coordinate_compression::coordinate_compression<T, Compare>` | ctor-explicit | 3 | 2 | 1,810 |
| `atl/ds/coordinate_compression.hpp:199` `atl::coordinate_compression::coordinate_compression<T, Compare>` | multi-arg ctor | 1 | 2 | 690 |
| `atl/ds/coordinate_compression.hpp:306` `atl::coordinate_compression::operator[]` | operator-other | 3 | 2 | 226 |
| `atl/ds/coordinate_compression.hpp:426` `atl::<deduction guide for coordinate_compression>` | deduction guide | 3 | 2 | 1,422 |
| `atl/ds/detail/bit_vector.hpp:55` `atl::detail::bit_vector::bit_vector` | default ctor | 1 | 1 | 32 |
| `atl/ds/detail/bit_vector.hpp:58` `atl::detail::bit_vector::bit_vector` | ctor-explicit | 1 | 1 | 81 |
| `atl/ds/detail/dynamic_forest.hpp:23` `atl::edge_values_t::edge_values_t` | default ctor | 1 | 4 | 140 |
| `atl/ds/detail/dynamic_forest.hpp:46` `atl::detail::forest_edge_hash::operator()` | operator() | 1 | 4 | 692 |
| `atl/ds/detail/hash_table.hpp:112` `atl::detail::hash_table::hash_table<Slot, Key, KeyOf, Hash, Eq>` | default ctor | 1 | 8 | 264 |
| `atl/ds/detail/hash_table.hpp:112` `atl::detail::hash_table::hash_table<Slot, Key, KeyOf, Hash, Eq>` | default ctor | 3 | 3 | 99 |
| `atl/ds/detail/hash_table.hpp:114` `atl::detail::hash_table::hash_table<Slot, Key, KeyOf, Hash, Eq>` | ctor-explicit | 1 | 1 | 92 |
| `atl/ds/detail/hash_table.hpp:114` `atl::detail::hash_table::hash_table<Slot, Key, KeyOf, Hash, Eq>` | ctor-explicit | 3 | 10 | 920 |
| `atl/ds/detail/hash_table.hpp:194` `atl::detail::hash_table::basic_iterator::basic_iterator<IsConst>` | default ctor | 3 | 11 | 407 |
| `atl/ds/detail/hash_table.hpp:196` `atl::detail::hash_table::basic_iterator::basic_iterator<IsConst>` | multi-arg ctor | 1 | 5 | 395 |
| `atl/ds/detail/hash_table.hpp:196` `atl::detail::hash_table::basic_iterator::basic_iterator<IsConst>` | multi-arg ctor | 3 | 6 | 474 |
| `atl/ds/detail/hash_table.hpp:198` `atl::detail::hash_table::basic_iterator::operator*` | operator-arithmetic | 3 | 11 | 704 |
| `atl/ds/detail/hash_table.hpp:200` `atl::detail::hash_table::basic_iterator::operator->` | operator-other | 1 | 5 | 320 |
| `atl/ds/detail/hash_table.hpp:200` `atl::detail::hash_table::basic_iterator::operator->` | operator-other | 3 | 6 | 384 |
| `atl/ds/detail/hash_table.hpp:202` `atl::detail::hash_table::basic_iterator::operator++` | operator-arithmetic | 3 | 11 | 935 |
| `atl/ds/detail/hash_table.hpp:208` `atl::detail::hash_table::basic_iterator::operator++` | operator-arithmetic | 3 | 11 | 1,188 |
| `atl/ds/detail/hash_table.hpp:214` `atl::detail::hash_table::basic_iterator::operator==` | operator-comparison | 1 | 4 | 316 |
| `atl/ds/detail/hash_table.hpp:214` `atl::detail::hash_table::basic_iterator::operator==` | operator-comparison | 2 | 1 | 79 |
| `atl/ds/detail/hash_table.hpp:214` `atl::detail::hash_table::basic_iterator::operator==` | operator-comparison | 3 | 6 | 474 |
| `atl/ds/detail/hash_table.hpp:30` `atl::detail::set_key_of::operator()` | operator() | 1 | 1 | 87 |
| `atl/ds/detail/hash_table.hpp:30` `atl::detail::set_key_of::operator()` | operator() | 3 | 4 | 348 |
| `atl/ds/detail/hash_table.hpp:35` `atl::detail::set_key_of::operator()` | operator() | 2 | 1 | 109 |
| `atl/ds/detail/hash_table.hpp:35` `atl::detail::set_key_of::operator()` | operator() | 3 | 4 | 436 |
| `atl/ds/detail/hash_table.hpp:44` `atl::detail::map_key_of::operator()` | operator() | 1 | 11 | 913 |
| `atl/ds/detail/hash_table.hpp:48` `atl::detail::map_key_of::operator()` | operator() | 1 | 11 | 1,045 |
| `atl/ds/detail/hashed_child_pool.hpp:74` `atl::detail::hashed_child_pool::hashed_child_pool<Node>` | default ctor | 1 | 5 | 215 |
| `atl/ds/detail/hashed_child_pool.hpp:85` `atl::detail::hashed_child_pool::operator[]` | operator-other | 1 | 5 | 240 |
| `atl/ds/detail/hashed_child_pool.hpp:87` `atl::detail::hashed_child_pool::operator[]` | operator-other | 1 | 5 | 300 |
| `atl/ds/detail/persistent_arena.hpp:34` `atl::detail::persistent_arena::persistent_arena<Node>` | default ctor | 1 | 3 | 117 |
| `atl/ds/detail/persistent_arena.hpp:34` `atl::detail::persistent_arena::persistent_arena<Node>` | default ctor | 3 | 7 | 273 |
| `atl/ds/detail/persistent_arena.hpp:38` `atl::detail::persistent_arena::persistent_arena<Node>` | ctor-explicit | 1 | 8 | 760 |
| `atl/ds/detail/persistent_arena.hpp:38` `atl::detail::persistent_arena::persistent_arena<Node>` | ctor-explicit | 3 | 2 | 190 |
| `atl/ds/detail/persistent_arena.hpp:47` `atl::detail::persistent_arena::operator[]` | operator-other | 1 | 10 | 600 |
| `atl/ds/detail/persistent_arena.hpp:49` `atl::detail::persistent_arena::operator[]` | operator-other | 1 | 8 | 576 |
| `atl/ds/detail/persistent_arena.hpp:49` `atl::detail::persistent_arena::operator[]` | operator-other | 3 | 2 | 144 |
| `atl/ds/detail/segment_tree_base.hpp:37` `atl::detail::segment_tree_base::segment_tree_base<T>` | default ctor | 1 | 6 | 180 |
| `atl/ds/difference_fenwick.hpp:85` `atl::difference_fenwick::difference_fenwick<Value, Op, Inverse>` | multi-arg ctor | 3 | 1 | 244 |
| `atl/ds/difference_fenwick.hpp:94` `atl::difference_fenwick::difference_fenwick<Value, Op, Inverse>` | multi-arg ctor | 1 | 1 | 391 |
| `atl/ds/direct_rmq.hpp:168` `atl::<deduction guide for direct_rmq>` | deduction guide | 2 | 2 | 538 |
| `atl/ds/direct_rmq.hpp:168` `atl::<deduction guide for direct_rmq>` | deduction guide | 3 | 1 | 269 |
| `atl/ds/direct_rmq.hpp:79` `atl::direct_rmq::direct_rmq<T, Compare>` | default ctor | 1 | 1 | 33 |
| `atl/ds/direct_rmq.hpp:79` `atl::direct_rmq::direct_rmq<T, Compare>` | default ctor | 3 | 2 | 66 |
| `atl/ds/direct_rmq.hpp:91` `atl::direct_rmq::direct_rmq<T, Compare>` | ctor-explicit | 1 | 3 | 5,298 |
| `atl/ds/dsu.hpp:30` `atl::dsu::dsu` | default ctor | 3 | 3 | 48 |
| `atl/ds/dsu.hpp:38` `atl::dsu::dsu` | ctor-explicit | 1 | 3 | 498 |
| `atl/ds/dual_segment_tree.hpp:111` `atl::dual_segment_tree::dual_segment_tree<Value, Tag, Mapping, Compose, TagOrder>` | multi-arg ctor | 1 | 1 | 530 |
| `atl/ds/dual_segment_tree.hpp:119` `atl::dual_segment_tree::dual_segment_tree<Value, Tag, Mapping, Compose, TagOrder>` | multi-arg ctor | 3 | 1 | 297 |
| `atl/ds/dual_segment_tree.hpp:127` `atl::dual_segment_tree::dual_segment_tree<Value, Tag, Mapping, Compose, TagOrder>` | multi-arg ctor | 1 | 1 | 410 |
| `atl/ds/dual_segment_tree.hpp:208` `atl::<deduction guide for dual_segment_tree>` | deduction guide | 3 | 1 | 221 |
| `atl/ds/dual_segment_tree.hpp:215` `atl::<deduction guide for dual_segment_tree>` | deduction guide | 3 | 1 | 266 |
| `atl/ds/dual_segment_tree.hpp:218` `atl::<deduction guide for dual_segment_tree>` | deduction guide | 3 | 1 | 263 |
| `atl/ds/dual_segment_tree.hpp:223` `atl::<deduction guide for dual_segment_tree>` | deduction guide | 2 | 1 | 308 |
| `atl/ds/dual_segment_tree.hpp:91` `atl::dual_segment_tree::dual_segment_tree<Value, Tag, Mapping, Compose, TagOrder>` | default ctor | 3 | 1 | 239 |
| `atl/ds/dual_segment_tree.hpp:97` `atl::dual_segment_tree::dual_segment_tree<Value, Tag, Mapping, Compose, TagOrder>` | multi-arg ctor | 3 | 1 | 316 |
| `atl/ds/dynamic_lazy_segment_tree.hpp:212` `atl::dynamic_lazy_segment_tree::dynamic_lazy_segment_tree<Value, Tag, Op, Mapping, Compose, Fill>` | default ctor | 2 | 1 | 367 |
| `atl/ds/dynamic_lazy_segment_tree.hpp:223` `atl::dynamic_lazy_segment_tree::dynamic_lazy_segment_tree<Value, Tag, Op, Mapping, Compose, Fill>` | multi-arg ctor | 1 | 1 | 522 |
| `atl/ds/dynamic_lazy_segment_tree.hpp:368` `atl::<deduction guide for dynamic_lazy_segment_tree>` | deduction guide | 3 | 1 | 308 |
| `atl/ds/dynamic_li_chao_tree.hpp:76` `atl::dynamic_li_chao_tree::dynamic_li_chao_tree<Coordinate, Compare, Function>` | default ctor | 2 | 1 | 43 |
| `atl/ds/dynamic_li_chao_tree.hpp:86` `atl::dynamic_li_chao_tree::dynamic_li_chao_tree<Coordinate, Compare, Function>` | multi-arg ctor | 1 | 1 | 214 |
| `atl/ds/dynamic_segment_tree.hpp:185` `atl::dynamic_segment_tree::dynamic_segment_tree<T, Op>` | default ctor | 2 | 1 | 151 |
| `atl/ds/dynamic_segment_tree.hpp:193` `atl::dynamic_segment_tree::dynamic_segment_tree<T, Op>` | multi-arg ctor | 1 | 1 | 197 |
| `atl/ds/dynamic_segment_tree.hpp:300` `atl::<deduction guide for dynamic_segment_tree>` | deduction guide | 3 | 1 | 118 |
| `atl/ds/euler_tour_tree.hpp:454` `atl::euler_tour_tree::euler_tour_tree<Value, Op, Tag, Mapping, Compose>` | default ctor | 3 | 1 | 303 |
| `atl/ds/euler_tour_tree.hpp:466` `atl::euler_tour_tree::euler_tour_tree<Value, Op, Tag, Mapping, Compose>` | ctor-explicit | 1 | 1 | 119 |
| `atl/ds/euler_tour_tree.hpp:477` `atl::euler_tour_tree::euler_tour_tree<Value, Op, Tag, Mapping, Compose>` | multi-arg ctor | 3 | 1 | 215 |
| `atl/ds/euler_tour_tree.hpp:490` `atl::euler_tour_tree::euler_tour_tree<Value, Op, Tag, Mapping, Compose>` | multi-arg ctor | 3 | 1 | 373 |
| `atl/ds/euler_tour_tree.hpp:501` `atl::euler_tour_tree::euler_tour_tree<Value, Op, Tag, Mapping, Compose>` | multi-arg ctor | 3 | 1 | 367 |
| `atl/ds/euler_tour_tree.hpp:511` `atl::euler_tour_tree::euler_tour_tree<Value, Op, Tag, Mapping, Compose>` | multi-arg ctor | 1 | 1 | 558 |
| `atl/ds/euler_tour_tree.hpp:776` `atl::<deduction guide for euler_tour_tree>` | deduction guide | 3 | 1 | 39 |
| `atl/ds/euler_tour_tree.hpp:778` `atl::<deduction guide for euler_tour_tree>` | deduction guide | 2 | 1 | 50 |
| `atl/ds/euler_tour_tree.hpp:781` `atl::<deduction guide for euler_tour_tree>` | deduction guide | 3 | 1 | 129 |
| `atl/ds/euler_tour_tree.hpp:785` `atl::<deduction guide for euler_tour_tree>` | deduction guide | 3 | 1 | 228 |
| `atl/ds/euler_tour_tree.hpp:789` `atl::<deduction guide for euler_tour_tree>` | deduction guide | 3 | 1 | 257 |
| `atl/ds/euler_tour_tree.hpp:794` `atl::<deduction guide for euler_tour_tree>` | deduction guide | 2 | 1 | 356 |
| `atl/ds/euler_tour_tree.hpp:84` `atl::detail::euler_tour_store::euler_tour_store<Value, Op, Tag, Mapping, Compose>` | default ctor | 1 | 1 | 39 |
| `atl/ds/euler_tour_tree.hpp:86` `atl::detail::euler_tour_store::euler_tour_store<Value, Op, Tag, Mapping, Compose>` | multi-arg ctor | 1 | 1 | 373 |
| `atl/ds/flat_hash_map.hpp:68` `atl::flat_hash_map::flat_hash_map<Key, T, Hash, Eq>` | default ctor | 1 | 8 | 288 |
| `atl/ds/flat_hash_map.hpp:68` `atl::flat_hash_map::flat_hash_map<Key, T, Hash, Eq>` | default ctor | 3 | 3 | 108 |
| `atl/ds/flat_hash_map.hpp:71` `atl::flat_hash_map::flat_hash_map<Key, T, Hash, Eq>` | ctor-explicit | 1 | 1 | 94 |
| `atl/ds/flat_hash_map.hpp:71` `atl::flat_hash_map::flat_hash_map<Key, T, Hash, Eq>` | ctor-explicit | 3 | 10 | 940 |
| `atl/ds/flat_hash_map.hpp:80` `atl::flat_hash_map::flat_hash_map<Key, T, Hash, Eq>` | ctor-explicit | 3 | 11 | 2,233 |
| `atl/ds/flat_hash_map.hpp:85` `atl::flat_hash_map::operator[]` | operator-other | 1 | 6 | 1,110 |
| `atl/ds/flat_hash_map.hpp:85` `atl::flat_hash_map::operator[]` | operator-other | 3 | 5 | 925 |
| `atl/ds/flat_hash_map.hpp:92` `atl::flat_hash_map::operator[]` | operator-other | 1 | 1 | 191 |
| `atl/ds/flat_hash_map.hpp:92` `atl::flat_hash_map::operator[]` | operator-other | 3 | 10 | 1,910 |
| `atl/ds/flat_hash_set.hpp:60` `atl::flat_hash_set::flat_hash_set<Key, Hash, Eq>` | default ctor | 1 | 1 | 36 |
| `atl/ds/flat_hash_set.hpp:63` `atl::flat_hash_set::flat_hash_set<Key, Hash, Eq>` | ctor-explicit | 3 | 1 | 94 |
| `atl/ds/flat_hash_set.hpp:72` `atl::flat_hash_set::flat_hash_set<Key, Hash, Eq>` | ctor-explicit | 3 | 1 | 243 |
| `atl/ds/hierarchical_bitset.hpp:36` `atl::hierarchical_bitset::hierarchical_bitset` | ctor-explicit | 1 | 1 | 391 |
| `atl/ds/hierarchical_bitset.hpp:62` `atl::hierarchical_bitset::hierarchical_bitset` | multi-arg ctor | 1 | 1 | 508 |
| `atl/ds/lazy_segment_tree.hpp:105` `atl::lazy_segment_tree::lazy_segment_tree<Value, Tag, Op, Mapping, Compose>` | default ctor | 3 | 1 | 273 |
| `atl/ds/lazy_segment_tree.hpp:111` `atl::lazy_segment_tree::lazy_segment_tree<Value, Tag, Op, Mapping, Compose>` | multi-arg ctor | 3 | 1 | 426 |
| `atl/ds/lazy_segment_tree.hpp:130` `atl::lazy_segment_tree::lazy_segment_tree<Value, Tag, Op, Mapping, Compose>` | multi-arg ctor | 1 | 1 | 736 |
| `atl/ds/lazy_segment_tree.hpp:277` `atl::<deduction guide for lazy_segment_tree>` | deduction guide | 3 | 1 | 247 |
| `atl/ds/lazy_segment_tree.hpp:286` `atl::<deduction guide for lazy_segment_tree>` | deduction guide | 2 | 1 | 411 |
| `atl/ds/li_chao_tree.hpp:74` `atl::li_chao_tree::li_chao_tree<Coordinate, Compare, Function>` | default ctor | 3 | 1 | 35 |
| `atl/ds/li_chao_tree.hpp:86` `atl::li_chao_tree::li_chao_tree<Coordinate, Compare, Function>` | multi-arg ctor | 3 | 1 | 196 |
| `atl/ds/li_chao_tree.hpp:99` `atl::li_chao_tree::li_chao_tree<Coordinate, Compare, Function>` | ctor-explicit | 1 | 1 | 414 |
| `atl/ds/linear_function.hpp:49` `atl::linear_function::operator()` | operator() | 1 | 1 | 96 |
| `atl/ds/link_cut_tree.hpp:1470` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 35 |
| `atl/ds/link_cut_tree.hpp:1472` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 46 |
| `atl/ds/link_cut_tree.hpp:1475` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 125 |
| `atl/ds/link_cut_tree.hpp:1479` `atl::<deduction guide for link_cut_tree>` | deduction guide | 2 | 1 | 224 |
| `atl/ds/link_cut_tree.hpp:1483` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 253 |
| `atl/ds/link_cut_tree.hpp:1488` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 352 |
| `atl/ds/link_cut_tree.hpp:1492` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 183 |
| `atl/ds/link_cut_tree.hpp:1496` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 282 |
| `atl/ds/link_cut_tree.hpp:1500` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 295 |
| `atl/ds/link_cut_tree.hpp:1506` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 394 |
| `atl/ds/link_cut_tree.hpp:1511` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 179 |
| `atl/ds/link_cut_tree.hpp:1515` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 291 |
| `atl/ds/link_cut_tree.hpp:1519` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 215 |
| `atl/ds/link_cut_tree.hpp:1524` `atl::<deduction guide for link_cut_tree>` | deduction guide | 3 | 1 | 325 |
| `atl/ds/link_cut_tree.hpp:162` `atl::detail::link_cut_store::link_cut_store<Layout, IsSized, Value, Op, Tag, Mapping, Compose, FoldOrder, Inverse>` | default ctor | 2 | 2 | 74 |
| `atl/ds/link_cut_tree.hpp:164` `atl::detail::link_cut_store::link_cut_store<Layout, IsSized, Value, Op, Tag, Mapping, Compose, FoldOrder, Inverse>` | multi-arg ctor | 1 | 2 | 760 |
| `atl/ds/link_cut_tree.hpp:514` `atl::detail::link_cut_rightmost_probe::operator()` | operator() | 1 | 2 | 150 |
| `atl/ds/link_cut_tree.hpp:840` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | default ctor | 1 | 1 | 312 |
| `atl/ds/link_cut_tree.hpp:840` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | default ctor | 3 | 1 | 312 |
| `atl/ds/link_cut_tree.hpp:852` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | ctor-explicit | 1 | 1 | 126 |
| `atl/ds/link_cut_tree.hpp:852` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | ctor-explicit | 3 | 1 | 126 |
| `atl/ds/link_cut_tree.hpp:863` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 1 | 1 | 238 |
| `atl/ds/link_cut_tree.hpp:863` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 238 |
| `atl/ds/link_cut_tree.hpp:877` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 2 | 1 | 424 |
| `atl/ds/link_cut_tree.hpp:877` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 424 |
| `atl/ds/link_cut_tree.hpp:887` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 1 | 1 | 247 |
| `atl/ds/link_cut_tree.hpp:887` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 247 |
| `atl/ds/link_cut_tree.hpp:896` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 1 | 1 | 407 |
| `atl/ds/link_cut_tree.hpp:896` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 407 |
| `atl/ds/link_cut_tree.hpp:907` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 1 | 1 | 375 |
| `atl/ds/link_cut_tree.hpp:907` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 375 |
| `atl/ds/link_cut_tree.hpp:917` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 2 | 1,176 |
| `atl/ds/link_cut_tree.hpp:925` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 1 | 1 | 228 |
| `atl/ds/link_cut_tree.hpp:925` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 228 |
| `atl/ds/link_cut_tree.hpp:933` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 2 | 836 |
| `atl/ds/link_cut_tree.hpp:938` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 1 | 1 | 447 |
| `atl/ds/link_cut_tree.hpp:938` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 447 |
| `atl/ds/link_cut_tree.hpp:949` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 2 | 1,296 |
| `atl/ds/link_cut_tree.hpp:956` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 1 | 1 | 224 |
| `atl/ds/link_cut_tree.hpp:956` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 224 |
| `atl/ds/link_cut_tree.hpp:961` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 1 | 1 | 443 |
| `atl/ds/link_cut_tree.hpp:961` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 443 |
| `atl/ds/link_cut_tree.hpp:969` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 1 | 1 | 273 |
| `atl/ds/link_cut_tree.hpp:969` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 273 |
| `atl/ds/link_cut_tree.hpp:975` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 1 | 1 | 481 |
| `atl/ds/link_cut_tree.hpp:975` `atl::detail::link_cut_tree_impl::link_cut_tree_impl<Engine, Layout, IsSized, IsEvertForm, Value, Op, Tag, Mapping, Compose, FoldOrder, ValuePlacement, Inverse>` | multi-arg ctor | 3 | 1 | 481 |
| `atl/ds/meldable_heap_forest.hpp:118` `atl::detail::heap_directory::heap_directory` | default ctor | 3 | 2 | 74 |
| `atl/ds/meldable_heap_forest.hpp:121` `atl::detail::heap_directory::heap_directory` | ctor-explicit | 1 | 2 | 324 |
| `atl/ds/meldable_heap_forest.hpp:30` `atl::shift_by_addition::operator()` | operator() | 1 | 1 | 253 |
| `atl/ds/meldable_heap_forest.hpp:30` `atl::shift_by_addition::operator()` | operator() | 3 | 1 | 253 |
| `atl/ds/meldable_heap_forest.hpp:411` `atl::meldable_heap_forest::meldable_heap_forest<T, Compare, Shift, Mapping, Compose, HasHandles>` | default ctor | 1 | 2 | 598 |
| `atl/ds/meldable_heap_forest.hpp:423` `atl::meldable_heap_forest::meldable_heap_forest<T, Compare, Shift, Mapping, Compose, HasHandles>` | multi-arg ctor | 3 | 2 | 708 |
| `atl/ds/meldable_heap_forest.hpp:44` `atl::compose_by_addition::operator()` | operator() | 1 | 2 | 522 |
| `atl/ds/persistent_adapters.hpp:174` `atl::persistent_deque::persistent_deque<T, Backing>` | multi-arg ctor | 1 | 1 | 126 |
| `atl/ds/persistent_adapters.hpp:205` `atl::persistent_deque::persistent_deque<T, Backing>` | default ctor | 3 | 1 | 45 |
| `atl/ds/persistent_adapters.hpp:210` `atl::persistent_deque::persistent_deque<T, Backing>` | ctor-explicit | 1 | 1 | 68 |
| `atl/ds/persistent_adapters.hpp:226` `atl::persistent_deque::operator[]` | operator-other | 3 | 1 | 217 |
| `atl/ds/persistent_adapters.hpp:67` `atl::persistent_queue::persistent_queue<T>` | multi-arg ctor | 1 | 1 | 112 |
| `atl/ds/persistent_adapters.hpp:71` `atl::persistent_queue::persistent_queue<T>` | default ctor | 1 | 1 | 29 |
| `atl/ds/persistent_array.hpp:191` `atl::persistent_array::persistent_array<T, Radix>` | multi-arg ctor | 1 | 3 | 306 |
| `atl/ds/persistent_array.hpp:203` `atl::persistent_array::persistent_array<T, Radix>` | default ctor | 2 | 3 | 165 |
| `atl/ds/persistent_array.hpp:215` `atl::persistent_array::persistent_array<T, Radix>` | ctor-explicit | 1 | 3 | 852 |
| `atl/ds/persistent_array.hpp:226` `atl::persistent_array::persistent_array<T, Radix>` | ctor-explicit | 1 | 3 | 342 |
| `atl/ds/persistent_array.hpp:244` `atl::persistent_array::operator[]` | operator-other | 1 | 3 | 390 |
| `atl/ds/persistent_array.hpp:266` `atl::persistent_array::iterator::iterator` | multi-arg ctor | 1 | 3 | 549 |
| `atl/ds/persistent_array.hpp:283` `atl::persistent_array::iterator::iterator` | default ctor | 2 | 3 | 63 |
| `atl/ds/persistent_array.hpp:287` `atl::persistent_array::iterator::operator*` | operator-arithmetic | 1 | 1 | 81 |
| `atl/ds/persistent_array.hpp:287` `atl::persistent_array::iterator::operator*` | operator-arithmetic | 2 | 2 | 162 |
| `atl/ds/persistent_array.hpp:291` `atl::persistent_array::iterator::operator->` | operator-other | 3 | 3 | 180 |
| `atl/ds/persistent_array.hpp:295` `atl::persistent_array::iterator::operator++` | operator-arithmetic | 1 | 1 | 179 |
| `atl/ds/persistent_array.hpp:295` `atl::persistent_array::iterator::operator++` | operator-arithmetic | 2 | 2 | 358 |
| `atl/ds/persistent_array.hpp:304` `atl::persistent_array::iterator::operator++` | operator-arithmetic | 2 | 2 | 172 |
| `atl/ds/persistent_array.hpp:304` `atl::persistent_array::iterator::operator++` | operator-arithmetic | 3 | 1 | 86 |
| `atl/ds/persistent_array.hpp:315` `atl::persistent_array::iterator::operator==` | operator-comparison | 1 | 1 | 85 |
| `atl/ds/persistent_array.hpp:315` `atl::persistent_array::iterator::operator==` | operator-comparison | 2 | 2 | 170 |
| `atl/ds/persistent_array.hpp:372` `atl::<deduction guide for persistent_array>` | deduction guide | 3 | 3 | 501 |
| `atl/ds/persistent_array.hpp:506` `atl::semi_persistent_array::semi_persistent_array<T, Radix>` | multi-arg ctor | 1 | 1 | 144 |
| `atl/ds/persistent_array.hpp:520` `atl::semi_persistent_array::semi_persistent_array<T, Radix>` | default ctor | 2 | 1 | 60 |
| `atl/ds/persistent_array.hpp:532` `atl::semi_persistent_array::semi_persistent_array<T, Radix>` | ctor-explicit | 1 | 1 | 289 |
| `atl/ds/persistent_array.hpp:543` `atl::semi_persistent_array::semi_persistent_array<T, Radix>` | ctor-explicit | 1 | 1 | 127 |
| `atl/ds/persistent_array.hpp:562` `atl::semi_persistent_array::operator[]` | operator-other | 1 | 1 | 140 |
| `atl/ds/persistent_array.hpp:608` `atl::<deduction guide for semi_persistent_array>` | deduction guide | 3 | 1 | 177 |
| `atl/ds/persistent_dsu.hpp:104` `atl::persistent_dsu::persistent_dsu<Backing>` | default ctor | 3 | 1 | 27 |
| `atl/ds/persistent_dsu.hpp:113` `atl::persistent_dsu::persistent_dsu<Backing>` | ctor-explicit | 1 | 1 | 107 |
| `atl/ds/persistent_dsu.hpp:77` `atl::persistent_dsu::persistent_dsu<Backing>` | multi-arg ctor | 1 | 1 | 100 |
| `atl/ds/persistent_heap.hpp:217` `atl::persistent_heap::persistent_heap<T, Compare>` | multi-arg ctor | 1 | 1 | 130 |
| `atl/ds/persistent_heap.hpp:276` `atl::persistent_heap::node_view::node_view` | multi-arg ctor | 3 | 1 | 68 |
| `atl/ds/persistent_heap.hpp:280` `atl::persistent_heap::node_view::node_view` | default ctor | 3 | 1 | 22 |
| `atl/ds/persistent_heap.hpp:305` `atl::persistent_heap::persistent_heap<T, Compare>` | default ctor | 1 | 1 | 114 |
| `atl/ds/persistent_heap.hpp:310` `atl::persistent_heap::persistent_heap<T, Compare>` | ctor-explicit | 3 | 1 | 90 |
| `atl/ds/persistent_heap.hpp:319` `atl::persistent_heap::persistent_heap<T, Compare>` | ctor-explicit | 3 | 1 | 304 |
| `atl/ds/persistent_heap.hpp:451` `atl::<deduction guide for persistent_heap>` | deduction guide | 3 | 1 | 110 |
| `atl/ds/persistent_heap.hpp:457` `atl::<deduction guide for persistent_heap>` | deduction guide | 3 | 1 | 279 |
| `atl/ds/persistent_heap.hpp:496` `atl::persistent_heap_forest::persistent_heap_forest<T, Compare>` | default ctor | 1 | 1 | 88 |
| `atl/ds/persistent_heap.hpp:68` `atl::detail::persistent_heap_pool::persistent_heap_pool<T, Compare>` | ctor-explicit | 1 | 1 | 241 |
| `atl/ds/persistent_lazy_segment_tree.hpp:184` `atl::persistent_lazy_segment_tree::persistent_lazy_segment_tree<Value, Tag, Op, Mapping, Compose, TagOrder>` | multi-arg ctor | 1 | 1 | 117 |
| `atl/ds/persistent_lazy_segment_tree.hpp:234` `atl::persistent_lazy_segment_tree::persistent_lazy_segment_tree<Value, Tag, Op, Mapping, Compose, TagOrder>` | default ctor | 2 | 1 | 285 |
| `atl/ds/persistent_lazy_segment_tree.hpp:240` `atl::persistent_lazy_segment_tree::persistent_lazy_segment_tree<Value, Tag, Op, Mapping, Compose, TagOrder>` | multi-arg ctor | 3 | 1 | 544 |
| `atl/ds/persistent_lazy_segment_tree.hpp:262` `atl::persistent_lazy_segment_tree::persistent_lazy_segment_tree<Value, Tag, Op, Mapping, Compose, TagOrder>` | multi-arg ctor | 1 | 1 | 917 |
| `atl/ds/persistent_lazy_segment_tree.hpp:277` `atl::persistent_lazy_segment_tree::persistent_lazy_segment_tree<Value, Tag, Op, Mapping, Compose, TagOrder>` | multi-arg ctor | 3 | 1 | 393 |
| `atl/ds/persistent_lazy_segment_tree.hpp:287` `atl::persistent_lazy_segment_tree::persistent_lazy_segment_tree<Value, Tag, Op, Mapping, Compose, TagOrder>` | multi-arg ctor | 1 | 1 | 634 |
| `atl/ds/persistent_lazy_segment_tree.hpp:416` `atl::<deduction guide for persistent_lazy_segment_tree>` | deduction guide | 3 | 1 | 281 |
| `atl/ds/persistent_lazy_segment_tree.hpp:426` `atl::<deduction guide for persistent_lazy_segment_tree>` | deduction guide | 3 | 1 | 384 |
| `atl/ds/persistent_lazy_segment_tree.hpp:430` `atl::<deduction guide for persistent_lazy_segment_tree>` | deduction guide | 3 | 1 | 321 |
| `atl/ds/persistent_lazy_segment_tree.hpp:436` `atl::<deduction guide for persistent_lazy_segment_tree>` | deduction guide | 2 | 1 | 424 |
| `atl/ds/persistent_lazy_segment_tree.hpp:83` `atl::persistent_lazy_segment_tree::pool::pool` | multi-arg ctor | 1 | 1 | 278 |
| `atl/ds/persistent_list.hpp:191` `atl::persistent_list::persistent_list<T>` | multi-arg ctor | 1 | 2 | 270 |
| `atl/ds/persistent_list.hpp:217` `atl::persistent_list::persistent_list<T>` | default ctor | 1 | 2 | 108 |
| `atl/ds/persistent_list.hpp:225` `atl::persistent_list::persistent_list<T>` | ctor-explicit | 3 | 2 | 1,288 |
| `atl/ds/persistent_list.hpp:240` `atl::persistent_list::persistent_list<T>` | ctor-explicit | 3 | 2 | 224 |
| `atl/ds/persistent_list.hpp:265` `atl::persistent_list::iterator::iterator` | multi-arg ctor | 3 | 2 | 344 |
| `atl/ds/persistent_list.hpp:282` `atl::persistent_list::iterator::iterator` | default ctor | 3 | 2 | 42 |
| `atl/ds/persistent_list.hpp:285` `atl::persistent_list::iterator::operator*` | operator-arithmetic | 3 | 2 | 184 |
| `atl/ds/persistent_list.hpp:288` `atl::persistent_list::iterator::operator->` | operator-other | 3 | 2 | 150 |
| `atl/ds/persistent_list.hpp:291` `atl::persistent_list::iterator::operator++` | operator-arithmetic | 3 | 2 | 714 |
| `atl/ds/persistent_list.hpp:305` `atl::persistent_list::iterator::operator++` | operator-arithmetic | 3 | 2 | 172 |
| `atl/ds/persistent_list.hpp:312` `atl::persistent_list::iterator::operator==` | operator-comparison | 3 | 2 | 186 |
| `atl/ds/persistent_list.hpp:331` `atl::persistent_list::operator[]` | operator-other | 1 | 2 | 250 |
| `atl/ds/persistent_list.hpp:409` `atl::<deduction guide for persistent_list>` | deduction guide | 3 | 2 | 244 |
| `atl/ds/persistent_segment_tree.hpp:112` `atl::persistent_segment_tree::persistent_segment_tree<T, Op>` | multi-arg ctor | 1 | 1 | 112 |
| `atl/ds/persistent_segment_tree.hpp:148` `atl::persistent_segment_tree::persistent_segment_tree<T, Op>` | default ctor | 2 | 1 | 141 |
| `atl/ds/persistent_segment_tree.hpp:153` `atl::persistent_segment_tree::persistent_segment_tree<T, Op>` | multi-arg ctor | 3 | 1 | 337 |
| `atl/ds/persistent_segment_tree.hpp:173` `atl::persistent_segment_tree::persistent_segment_tree<T, Op>` | multi-arg ctor | 1 | 1 | 706 |
| `atl/ds/persistent_segment_tree.hpp:328` `atl::<deduction guide for persistent_segment_tree>` | deduction guide | 3 | 1 | 129 |
| `atl/ds/persistent_segment_tree.hpp:336` `atl::<deduction guide for persistent_segment_tree>` | deduction guide | 2 | 1 | 224 |
| `atl/ds/persistent_segment_tree.hpp:54` `atl::persistent_segment_tree::pool::pool` | multi-arg ctor | 1 | 1 | 103 |
| `atl/ds/persistent_treap_sequence.hpp:108` `atl::persistent_treap_sequence::pool::pool` | default ctor | 2 | 1 | 79 |
| `atl/ds/persistent_treap_sequence.hpp:110` `atl::persistent_treap_sequence::pool::pool` | multi-arg ctor | 1 | 1 | 277 |
| `atl/ds/persistent_treap_sequence.hpp:117` `atl::persistent_treap_sequence::pool::pool` | multi-arg ctor | 2 | 1 | 433 |
| `atl/ds/persistent_treap_sequence.hpp:363` `atl::persistent_treap_sequence::persistent_treap_sequence<T, Op, F, Mapping, Compose>` | multi-arg ctor | 1 | 1 | 114 |
| `atl/ds/persistent_treap_sequence.hpp:538` `atl::persistent_treap_sequence::persistent_treap_sequence<T, Op, F, Mapping, Compose>` | default ctor | 2 | 1 | 88 |
| `atl/ds/persistent_treap_sequence.hpp:546` `atl::persistent_treap_sequence::persistent_treap_sequence<T, Op, F, Mapping, Compose>` | ctor-explicit | 2 | 1 | 490 |
| `atl/ds/persistent_treap_sequence.hpp:560` `atl::persistent_treap_sequence::persistent_treap_sequence<T, Op, F, Mapping, Compose>` | multi-arg ctor | 1 | 1 | 665 |
| `atl/ds/persistent_treap_sequence.hpp:573` `atl::persistent_treap_sequence::persistent_treap_sequence<T, Op, F, Mapping, Compose>` | multi-arg ctor | 2 | 1 | 1,036 |
| `atl/ds/persistent_treap_sequence.hpp:903` `atl::<deduction guide for persistent_treap_sequence>` | deduction guide | 3 | 1 | 130 |
| `atl/ds/persistent_treap_sequence.hpp:907` `atl::<deduction guide for persistent_treap_sequence>` | deduction guide | 2 | 1 | 223 |
| `atl/ds/persistent_treap_sequence.hpp:912` `atl::<deduction guide for persistent_treap_sequence>` | deduction guide | 3 | 1 | 332 |
| `atl/ds/persistent_vector.hpp:1021` `atl::persistent_vector::iterator::iterator` | multi-arg ctor | 3 | 1 | 134 |
| `atl/ds/persistent_vector.hpp:1047` `atl::persistent_vector::iterator::iterator` | default ctor | 3 | 1 | 21 |
| `atl/ds/persistent_vector.hpp:1050` `atl::persistent_vector::iterator::operator*` | operator-arithmetic | 3 | 1 | 63 |
| `atl/ds/persistent_vector.hpp:1053` `atl::persistent_vector::iterator::operator->` | operator-other | 3 | 1 | 61 |
| `atl/ds/persistent_vector.hpp:1056` `atl::persistent_vector::iterator::operator++` | operator-arithmetic | 3 | 1 | 115 |
| `atl/ds/persistent_vector.hpp:1064` `atl::persistent_vector::iterator::operator++` | operator-arithmetic | 3 | 1 | 86 |
| `atl/ds/persistent_vector.hpp:1071` `atl::persistent_vector::iterator::operator==` | operator-comparison | 3 | 1 | 85 |
| `atl/ds/persistent_vector.hpp:1222` `atl::<deduction guide for persistent_vector>` | deduction guide | 2 | 1 | 126 |
| `atl/ds/persistent_vector.hpp:1249` `atl::semi_persistent_vector::semi_persistent_vector<T, Radix>` | multi-arg ctor | 1 | 1 | 133 |
| `atl/ds/persistent_vector.hpp:1277` `atl::semi_persistent_vector::semi_persistent_vector<T, Radix>` | default ctor | 1 | 1 | 61 |
| `atl/ds/persistent_vector.hpp:1284` `atl::semi_persistent_vector::semi_persistent_vector<T, Radix>` | ctor-explicit | 3 | 1 | 257 |
| `atl/ds/persistent_vector.hpp:1292` `atl::semi_persistent_vector::semi_persistent_vector<T, Radix>` | ctor-explicit | 3 | 1 | 129 |
| `atl/ds/persistent_vector.hpp:1305` `atl::semi_persistent_vector::operator[]` | operator-other | 1 | 1 | 134 |
| `atl/ds/persistent_vector.hpp:1381` `atl::<deduction guide for semi_persistent_vector>` | deduction guide | 3 | 1 | 136 |
| `atl/ds/persistent_vector.hpp:917` `atl::persistent_vector::persistent_vector<T, Radix>` | multi-arg ctor | 1 | 1 | 128 |
| `atl/ds/persistent_vector.hpp:967` `atl::persistent_vector::persistent_vector<T, Radix>` | default ctor | 3 | 1 | 56 |
| `atl/ds/persistent_vector.hpp:974` `atl::persistent_vector::persistent_vector<T, Radix>` | ctor-explicit | 1 | 1 | 252 |
| `atl/ds/persistent_vector.hpp:982` `atl::persistent_vector::persistent_vector<T, Radix>` | ctor-explicit | 3 | 1 | 116 |
| `atl/ds/persistent_vector.hpp:994` `atl::persistent_vector::operator[]` | operator-other | 1 | 1 | 134 |
| `atl/ds/rollback_dsu.hpp:56` `atl::rollback_dsu::rollback_dsu` | default ctor | 3 | 2 | 50 |
| `atl/ds/rollback_dsu.hpp:64` `atl::rollback_dsu::rollback_dsu` | ctor-explicit | 1 | 2 | 402 |
| `atl/ds/rollback_weighted_dsu.hpp:119` `atl::rollback_weighted_dsu::rollback_weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 1 | 1 | 484 |
| `atl/ds/rollback_weighted_dsu.hpp:131` `atl::rollback_weighted_dsu::rollback_weighted_dsu<Potential, Op, Inverse>` | default ctor | 3 | 1 | 34 |
| `atl/ds/rollback_weighted_dsu.hpp:141` `atl::rollback_weighted_dsu::rollback_weighted_dsu<Potential, Op, Inverse>` | ctor-explicit | 1 | 1 | 281 |
| `atl/ds/rollback_weighted_dsu.hpp:155` `atl::rollback_weighted_dsu::rollback_weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 3 | 1 | 290 |
| `atl/ds/rollback_weighted_dsu.hpp:166` `atl::rollback_weighted_dsu::rollback_weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 3 | 1 | 287 |
| `atl/ds/rollback_weighted_dsu.hpp:179` `atl::rollback_weighted_dsu::rollback_weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 3 | 1 | 297 |
| `atl/ds/segment_tree.hpp:233` `atl::<deduction guide for segment_tree>` | deduction guide | 3 | 3 | 306 |
| `atl/ds/segment_tree.hpp:241` `atl::<deduction guide for segment_tree>` | deduction guide | 2 | 1 | 254 |
| `atl/ds/segment_tree.hpp:241` `atl::<deduction guide for segment_tree>` | deduction guide | 3 | 2 | 508 |
| `atl/ds/segment_tree.hpp:66` `atl::segment_tree::segment_tree<T, Op>` | default ctor | 2 | 1 | 129 |
| `atl/ds/segment_tree.hpp:66` `atl::segment_tree::segment_tree<T, Op>` | default ctor | 3 | 2 | 258 |
| `atl/ds/segment_tree.hpp:71` `atl::segment_tree::segment_tree<T, Op>` | multi-arg ctor | 2 | 1 | 146 |
| `atl/ds/segment_tree.hpp:71` `atl::segment_tree::segment_tree<T, Op>` | multi-arg ctor | 3 | 2 | 292 |
| `atl/ds/segment_tree.hpp:83` `atl::segment_tree::segment_tree<T, Op>` | multi-arg ctor | 1 | 3 | 1,353 |
| `atl/ds/segment_tree_beats.hpp:1452` `atl::beats_segment_tree::beats_segment_tree<Element, Sum>` | default ctor | 3 | 1 | 70 |
| `atl/ds/segment_tree_beats.hpp:1455` `atl::beats_segment_tree::beats_segment_tree<Element, Sum>` | ctor-explicit | 3 | 1 | 202 |
| `atl/ds/segment_tree_beats.hpp:1468` `atl::beats_segment_tree::beats_segment_tree<Element, Sum>` | ctor-explicit | 1 | 1 | 390 |
| `atl/ds/segment_tree_beats.hpp:207` `atl::beats::extremes::operator==` | operator-comparison | 3 | 1 | 60 |
| `atl/ds/segment_tree_beats.hpp:262` `atl::beats::mod_sum::operator==` | operator-comparison | 3 | 1 | 59 |
| `atl/ds/segment_tree_beats.hpp:282` `atl::beats::assign::operator==` | operator-comparison | 3 | 1 | 58 |
| `atl/ds/segment_tree_beats.hpp:314` `atl::beats::sqrt_add_sum::operator==` | operator-comparison | 3 | 1 | 64 |
| `atl/ds/segment_tree_beats.hpp:333` `atl::beats::add::operator==` | operator-comparison | 3 | 1 | 55 |
| `atl/ds/segment_tree_beats.hpp:362` `atl::beats::bits_max::operator==` | operator-comparison | 3 | 1 | 60 |
| `atl/ds/segment_tree_beats.hpp:390` `atl::beats::merge::operator()` | operator() | 1 | 1 | 1,089 |
| `atl/ds/segment_tree_beats.hpp:413` `atl::beats::merge::operator()` | operator() | 3 | 1 | 303 |
| `atl/ds/segment_tree_beats.hpp:419` `atl::beats::merge::operator()` | operator() | 3 | 1 | 332 |
| `atl/ds/segment_tree_beats.hpp:425` `atl::beats::merge::operator()` | operator() | 3 | 1 | 276 |
| `atl/ds/sparse_table.hpp:116` `atl::disjoint_sparse_table::disjoint_sparse_table<T, Op>` | default ctor | 3 | 1 | 44 |
| `atl/ds/sparse_table.hpp:124` `atl::disjoint_sparse_table::disjoint_sparse_table<T, Op>` | ctor-explicit | 1 | 1 | 1,257 |
| `atl/ds/sparse_table.hpp:173` `atl::<deduction guide for disjoint_sparse_table>` | deduction guide | 2 | 1 | 143 |
| `atl/ds/sparse_table.hpp:44` `atl::sparse_table::sparse_table<T, Op>` | default ctor | 3 | 1 | 35 |
| `atl/ds/sparse_table.hpp:52` `atl::sparse_table::sparse_table<T, Op>` | ctor-explicit | 1 | 1 | 863 |
| `atl/ds/sparse_table.hpp:86` `atl::<deduction guide for sparse_table>` | deduction guide | 2 | 1 | 125 |
| `atl/ds/splay_engine.hpp:185` `atl::detail::splay_composites::splay_composites<Engine>` | default ctor | 3 | 6 | 174 |
| `atl/ds/splay_map.hpp:107` `atl::detail::splay_map_pool::splay_map_pool<Layout, Key, Value, Compare, Op>` | default ctor | 2 | 1 | 222 |
| `atl/ds/splay_map.hpp:114` `atl::detail::splay_map_pool::splay_map_pool<Layout, Key, Value, Compare, Op>` | ctor-explicit | 1 | 1 | 305 |
| `atl/ds/splay_map.hpp:310` `atl::detail::splay_map_adopt_t::splay_map_adopt_t` | default ctor | 1 | 1 | 39 |
| `atl/ds/splay_map.hpp:560` `atl::detail::splay_map_impl::splay_map_impl<Derived, Engine, Layout, Key, Value, Compare, Op>` | multi-arg ctor | 1 | 1 | 123 |
| `atl/ds/splay_map.hpp:568` `atl::detail::splay_map_impl::splay_map_impl<Derived, Engine, Layout, Key, Value, Compare, Op>` | default ctor | 1 | 1 | 118 |
| `atl/ds/splay_map.hpp:573` `atl::detail::splay_map_impl::splay_map_impl<Derived, Engine, Layout, Key, Value, Compare, Op>` | ctor-explicit | 1 | 1 | 126 |
| `atl/ds/splay_map.hpp:585` `atl::detail::splay_map_impl::splay_map_impl<Derived, Engine, Layout, Key, Value, Compare, Op>` | multi-arg ctor | 2 | 1 | 280 |
| `atl/ds/splay_map.hpp:601` `atl::detail::splay_map_impl::splay_map_impl<Derived, Engine, Layout, Key, Value, Compare, Op>` | multi-arg ctor | 3 | 1 | 366 |
| `atl/ds/splay_map.hpp:614` `atl::detail::splay_map_impl::splay_map_impl<Derived, Engine, Layout, Key, Value, Compare, Op>` | multi-arg ctor | 3 | 1 | 516 |
| `atl/ds/splay_map.hpp:620` `atl::detail::splay_map_impl::splay_map_impl<Derived, Engine, Layout, Key, Value, Compare, Op>` | copy/move ctor | 1 | 1 | 95 |
| `atl/ds/splay_map.hpp:623` `atl::detail::splay_map_impl::splay_map_impl<Derived, Engine, Layout, Key, Value, Compare, Op>` | copy/move ctor | 1 | 1 | 104 |
| `atl/ds/splay_map.hpp:626` `atl::detail::splay_map_impl::operator=` | copy/move assignment | 3 | 1 | 204 |
| `atl/ds/splay_map.hpp:636` `atl::detail::splay_map_impl::operator=` | copy/move assignment | 3 | 1 | 219 |
| `atl/ds/splay_map.hpp:647` `atl::detail::splay_map_impl::~splay_map_impl<Derived, Engine, Layout, Key, Value, Compare, Op>` | destructor | 2 | 1 | 62 |
| `atl/ds/splay_sequence.hpp:1037` `atl::<deduction guide for splay_sequence>` | deduction guide | 3 | 1 | 108 |
| `atl/ds/splay_sequence.hpp:1040` `atl::<deduction guide for splay_sequence>` | deduction guide | 3 | 1 | 74 |
| `atl/ds/splay_sequence.hpp:1043` `atl::<deduction guide for splay_sequence>` | deduction guide | 3 | 1 | 106 |
| `atl/ds/splay_sequence.hpp:1047` `atl::<deduction guide for splay_sequence>` | deduction guide | 3 | 1 | 201 |
| `atl/ds/splay_sequence.hpp:1050` `atl::<deduction guide for splay_sequence>` | deduction guide | 3 | 1 | 211 |
| `atl/ds/splay_sequence.hpp:1055` `atl::<deduction guide for splay_sequence>` | deduction guide | 3 | 1 | 310 |
| `atl/ds/splay_sequence.hpp:129` `atl::detail::splay_sequence_pool::splay_sequence_pool<Layout, T, Op, F, Mapping, Compose, IsReversible>` | default ctor | 1 | 1 | 438 |
| `atl/ds/splay_sequence.hpp:454` `atl::detail::splay_sequence_adopt_t::splay_sequence_adopt_t` | default ctor | 1 | 1 | 44 |
| `atl/ds/splay_sequence.hpp:592` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 1 | 1 | 133 |
| `atl/ds/splay_sequence.hpp:596` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | default ctor | 1 | 1 | 95 |
| `atl/ds/splay_sequence.hpp:601` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 1 | 1 | 261 |
| `atl/ds/splay_sequence.hpp:611` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | ctor-explicit | 3 | 1 | 315 |
| `atl/ds/splay_sequence.hpp:618` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 3 | 1 | 477 |
| `atl/ds/splay_sequence.hpp:631` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 2 | 1 | 525 |
| `atl/ds/splay_sequence.hpp:642` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 3 | 1 | 799 |
| `atl/ds/splay_sequence.hpp:657` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 3 | 1 | 861 |
| `atl/ds/splay_sequence.hpp:664` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | copy/move ctor | 1 | 1 | 105 |
| `atl/ds/splay_sequence.hpp:667` `atl::detail::splay_sequence_impl::splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | copy/move ctor | 1 | 1 | 114 |
| `atl/ds/splay_sequence.hpp:670` `atl::detail::splay_sequence_impl::operator=` | copy/move assignment | 3 | 1 | 214 |
| `atl/ds/splay_sequence.hpp:680` `atl::detail::splay_sequence_impl::operator=` | copy/move assignment | 3 | 1 | 229 |
| `atl/ds/splay_sequence.hpp:691` `atl::detail::splay_sequence_impl::~splay_sequence_impl<Derived, Engine, Layout, T, Op, F, Mapping, Compose, IsReversible>` | destructor | 2 | 1 | 67 |
| `atl/ds/static_vector.hpp:199` `atl::static_vector::operator[]` | operator-other | 3 | 19 | 1,938 |
| `atl/ds/static_vector.hpp:205` `atl::static_vector::operator[]` | operator-other | 1 | 1 | 114 |
| `atl/ds/static_vector.hpp:205` `atl::static_vector::operator[]` | operator-other | 3 | 18 | 2,052 |
| `atl/ds/static_vector.hpp:68` `atl::static_vector::static_vector<T, Capacity>` | default ctor | 1 | 3 | 108 |
| `atl/ds/static_vector.hpp:68` `atl::static_vector::static_vector<T, Capacity>` | default ctor | 2 | 15 | 540 |
| `atl/ds/static_vector.hpp:68` `atl::static_vector::static_vector<T, Capacity>` | default ctor | 3 | 1 | 36 |
| `atl/ds/static_vector.hpp:74` `atl::static_vector::static_vector<T, Capacity>` | ctor-implicit-converting | 1 | 1 | 109 |
| `atl/ds/static_vector.hpp:74` `atl::static_vector::static_vector<T, Capacity>` | ctor-implicit-converting | 2 | 1 | 109 |
| `atl/ds/static_vector.hpp:74` `atl::static_vector::static_vector<T, Capacity>` | ctor-implicit-converting | 3 | 17 | 1,853 |
| `atl/ds/static_vector.hpp:88` `atl::static_vector::static_vector<T, Capacity>` | ctor-explicit | 2 | 1 | 343 |
| `atl/ds/static_vector.hpp:88` `atl::static_vector::static_vector<T, Capacity>` | ctor-explicit | 3 | 18 | 6,174 |
| `atl/ds/subtree_link_cut_tree.hpp:216` `atl::<deduction guide for subtree_link_cut_tree>` | deduction guide | 3 | 1 | 193 |
| `atl/ds/subtree_link_cut_tree.hpp:220` `atl::<deduction guide for subtree_link_cut_tree>` | deduction guide | 2 | 1 | 292 |
| `atl/ds/subtree_link_cut_tree.hpp:80` `atl::detail::subtree_link_cut_tree_impl::subtree_link_cut_tree_impl<Layout, Value, Op, Inverse>` | default ctor | 1 | 1 | 201 |
| `atl/ds/subtree_link_cut_tree.hpp:89` `atl::detail::subtree_link_cut_tree_impl::subtree_link_cut_tree_impl<Layout, Value, Op, Inverse>` | multi-arg ctor | 1 | 1 | 183 |
| `atl/ds/subtree_link_cut_tree.hpp:99` `atl::detail::subtree_link_cut_tree_impl::subtree_link_cut_tree_impl<Layout, Value, Op, Inverse>` | multi-arg ctor | 2 | 1 | 345 |
| `atl/ds/time_divide_and_conquer.hpp:100` `atl::every_time::operator()` | operator() | 3 | 1 | 65 |
| `atl/ds/time_divide_and_conquer.hpp:244` `atl::detail::rollback_frames::rollback_frames<Structure, Save, Restore>` | multi-arg ctor | 1 | 1 | 176 |
| `atl/ds/time_divide_and_conquer.hpp:279` `atl::detail::copy_frames::copy_frames<Structure>` | multi-arg ctor | 3 | 1 | 108 |
| `atl/ds/time_divide_and_conquer.hpp:37` `atl::item_lifetime::operator==` | operator-comparison | 3 | 1 | 65 |
| `atl/ds/time_divide_and_conquer.hpp:86` `atl::by_copy_t::by_copy_t` | default ctor | 1 | 1 | 31 |
| `atl/ds/top_clusters.hpp:119` `atl::top_clusters::sum::compress::operator()` | operator() | 3 | 1 | 186 |
| `atl/ds/top_clusters.hpp:127` `atl::top_clusters::sum::rake::operator()` | operator() | 3 | 1 | 118 |
| `atl/ds/top_clusters.hpp:133` `atl::top_clusters::sum::add_vertex::operator()` | operator() | 3 | 1 | 123 |
| `atl/ds/top_clusters.hpp:139` `atl::top_clusters::sum::add_edge::operator()` | operator() | 3 | 1 | 101 |
| `atl/ds/top_clusters.hpp:145` `atl::top_clusters::sum::mapping::operator()` | operator() | 3 | 1 | 103 |
| `atl/ds/top_clusters.hpp:151` `atl::top_clusters::sum::map_path::operator()` | operator() | 3 | 1 | 192 |
| `atl/ds/top_clusters.hpp:159` `atl::top_clusters::sum::map_point::operator()` | operator() | 3 | 1 | 120 |
| `atl/ds/top_clusters.hpp:165` `atl::top_clusters::sum::compose::operator()` | operator() | 3 | 1 | 115 |
| `atl/ds/top_clusters.hpp:182` `atl::top_clusters::extremes_aggregate::operator==` | operator-comparison | 3 | 1 | 84 |
| `atl/ds/top_clusters.hpp:198` `atl::top_clusters::extremes_path::operator==` | operator-comparison | 3 | 1 | 79 |
| `atl/ds/top_clusters.hpp:218` `atl::top_clusters::extremes_tag::operator==` | operator-comparison | 1 | 1 | 78 |
| `atl/ds/top_clusters.hpp:260` `atl::top_clusters::extremes::compress::operator()` | operator() | 1 | 1 | 186 |
| `atl/ds/top_clusters.hpp:268` `atl::top_clusters::extremes::rake::operator()` | operator() | 1 | 1 | 118 |
| `atl/ds/top_clusters.hpp:274` `atl::top_clusters::extremes::add_vertex::operator()` | operator() | 1 | 1 | 144 |
| `atl/ds/top_clusters.hpp:282` `atl::top_clusters::extremes::add_edge::operator()` | operator() | 1 | 1 | 101 |
| `atl/ds/top_clusters.hpp:288` `atl::top_clusters::extremes::mapping::operator()` | operator() | 1 | 1 | 132 |
| `atl/ds/top_clusters.hpp:294` `atl::top_clusters::extremes::map_path::operator()` | operator() | 1 | 1 | 184 |
| `atl/ds/top_clusters.hpp:302` `atl::top_clusters::extremes::map_point::operator()` | operator() | 1 | 1 | 116 |
| `atl/ds/top_clusters.hpp:308` `atl::top_clusters::extremes::compose::operator()` | operator() | 1 | 1 | 178 |
| `atl/ds/top_clusters.hpp:326` `atl::top_clusters::farthest_vertex::operator==` | operator-comparison | 3 | 1 | 81 |
| `atl/ds/top_clusters.hpp:337` `atl::top_clusters::diameter_span::operator==` | operator-comparison | 3 | 1 | 79 |
| `atl/ds/top_clusters.hpp:351` `atl::top_clusters::diameter_value::diameter_value<W>` | default ctor | 1 | 1 | 37 |
| `atl/ds/top_clusters.hpp:354` `atl::top_clusters::diameter_value::diameter_value<W>` | ctor-implicit-converting | 1 | 1 | 75 |
| `atl/ds/top_clusters.hpp:364` `atl::top_clusters::diameter_value::operator==` | operator-comparison | 3 | 1 | 80 |
| `atl/ds/top_clusters.hpp:380` `atl::top_clusters::diameter_path::operator==` | operator-comparison | 3 | 1 | 79 |
| `atl/ds/top_clusters.hpp:390` `atl::top_clusters::diameter_point::operator==` | operator-comparison | 3 | 1 | 80 |
| `atl/ds/top_clusters.hpp:448` `atl::top_clusters::diameter::compress::operator()` | operator() | 1 | 1 | 384 |
| `atl/ds/top_clusters.hpp:458` `atl::top_clusters::diameter::rake::operator()` | operator() | 1 | 1 | 214 |
| `atl/ds/top_clusters.hpp:466` `atl::top_clusters::diameter::add_vertex::operator()` | operator() | 1 | 1 | 511 |
| `atl/ds/top_clusters.hpp:478` `atl::top_clusters::diameter::add_edge::operator()` | operator() | 1 | 1 | 120 |
| `atl/ds/top_clusters.hpp:484` `atl::top_clusters::diameter::reverse::operator()` | operator() | 1 | 1 | 132 |
| `atl/ds/top_clusters.hpp:524` `atl::top_clusters::marked_path::operator==` | operator-comparison | 3 | 1 | 77 |
| `atl/ds/top_clusters.hpp:55` `atl::top_clusters::sum_aggregate::operator==` | operator-comparison | 3 | 1 | 79 |
| `atl/ds/top_clusters.hpp:72` `atl::top_clusters::sum_path::operator==` | operator-comparison | 3 | 1 | 74 |
| `atl/ds/top_clusters.hpp:84` `atl::top_clusters::sum_tag::operator==` | operator-comparison | 3 | 1 | 73 |
| `atl/ds/top_tree.hpp:1511` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | default ctor | 1 | 1 | 548 |
| `atl/ds/top_tree.hpp:1524` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | multi-arg ctor | 1 | 1 | 503 |
| `atl/ds/top_tree.hpp:1533` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | multi-arg ctor | 1 | 1 | 531 |
| `atl/ds/top_tree.hpp:1547` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | multi-arg ctor | 1 | 1 | 779 |
| `atl/ds/top_tree.hpp:1559` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | multi-arg ctor | 1 | 1 | 827 |
| `atl/ds/top_tree.hpp:1578` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | multi-arg ctor | 3 | 1 | 748 |
| `atl/ds/top_tree.hpp:1589` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | multi-arg ctor | 3 | 1 | 807 |
| `atl/ds/top_tree.hpp:1601` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | multi-arg ctor | 3 | 1 | 1,152 |
| `atl/ds/top_tree.hpp:1617` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | multi-arg ctor | 3 | 1 | 1,231 |
| `atl/ds/top_tree.hpp:1637` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | ctor-explicit | 1 | 1 | 746 |
| `atl/ds/top_tree.hpp:1657` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | ctor-explicit | 2 | 1 | 818 |
| `atl/ds/top_tree.hpp:1664` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | ctor-explicit | 3 | 1 | 225 |
| `atl/ds/top_tree.hpp:1669` `atl::detail::top_tree_impl::top_tree_impl<Layout, IsChainPushed, IsRakeRootInsert, IsEdgeLeaf, IsEvertForm, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose, ValuePlacement>` | ctor-explicit | 3 | 1 | 221 |
| `atl/ds/top_tree.hpp:185` `atl::detail::top_pool::top_pool<Layout, IsOwnCached, Value, Path, Point, Compress, Rake, AddVertex, AddEdge, Reverse, Tag, Mapping, MapPath, MapPoint, Compose>` | multi-arg ctor | 1 | 1 | 749 |
| `atl/ds/top_tree.hpp:2269` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 296 |
| `atl/ds/top_tree.hpp:2275` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 384 |
| `atl/ds/top_tree.hpp:2280` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 478 |
| `atl/ds/top_tree.hpp:2287` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 576 |
| `atl/ds/top_tree.hpp:2294` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 313 |
| `atl/ds/top_tree.hpp:2300` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 401 |
| `atl/ds/top_tree.hpp:2306` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 504 |
| `atl/ds/top_tree.hpp:2314` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 586 |
| `atl/ds/top_tree.hpp:2321` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 336 |
| `atl/ds/top_tree.hpp:2326` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 525 |
| `atl/ds/top_tree.hpp:2332` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 353 |
| `atl/ds/top_tree.hpp:2338` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 538 |
| `atl/ds/top_tree.hpp:2345` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 362 |
| `atl/ds/top_tree.hpp:2351` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 444 |
| `atl/ds/top_tree.hpp:2356` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 520 |
| `atl/ds/top_tree.hpp:2364` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 607 |
| `atl/ds/top_tree.hpp:2370` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 401 |
| `atl/ds/top_tree.hpp:2376` `atl::<deduction guide for top_tree>` | deduction guide | 3 | 1 | 558 |
| `atl/ds/top_tree.hpp:531` `atl::detail::top_named_members::top_named_members<Derived, Value, Path, Tag>` | default ctor | 1 | 1 | 30 |
| `atl/ds/treap_map.hpp:106` `atl::treap_map::pool::pool` | ctor-explicit | 1 | 1 | 370 |
| `atl/ds/treap_map.hpp:220` `atl::treap_map::treap_map<Key, Value, Compare, Op>` | multi-arg ctor | 3 | 1 | 95 |
| `atl/ds/treap_map.hpp:361` `atl::treap_map::treap_map<Key, Value, Compare, Op>` | default ctor | 2 | 1 | 111 |
| `atl/ds/treap_map.hpp:366` `atl::treap_map::treap_map<Key, Value, Compare, Op>` | ctor-explicit | 3 | 1 | 119 |
| `atl/ds/treap_map.hpp:378` `atl::treap_map::treap_map<Key, Value, Compare, Op>` | multi-arg ctor | 1 | 1 | 269 |
| `atl/ds/treap_map.hpp:393` `atl::treap_map::treap_map<Key, Value, Compare, Op>` | multi-arg ctor | 3 | 1 | 374 |
| `atl/ds/treap_map.hpp:406` `atl::treap_map::treap_map<Key, Value, Compare, Op>` | multi-arg ctor | 3 | 1 | 523 |
| `atl/ds/treap_map.hpp:412` `atl::treap_map::treap_map<Key, Value, Compare, Op>` | copy/move ctor | 2 | 1 | 95 |
| `atl/ds/treap_map.hpp:415` `atl::treap_map::treap_map<Key, Value, Compare, Op>` | copy/move ctor | 2 | 1 | 94 |
| `atl/ds/treap_map.hpp:418` `atl::treap_map::operator=` | copy/move assignment | 2 | 1 | 195 |
| `atl/ds/treap_map.hpp:428` `atl::treap_map::operator=` | copy/move assignment | 2 | 1 | 201 |
| `atl/ds/treap_map.hpp:439` `atl::treap_map::~treap_map<Key, Value, Compare, Op>` | destructor | 2 | 1 | 55 |
| `atl/ds/treap_map.hpp:98` `atl::treap_map::pool::pool` | default ctor | 2 | 1 | 277 |
| `atl/ds/treap_sequence.hpp:122` `atl::treap_sequence::pool::pool` | default ctor | 1 | 1 | 634 |
| `atl/ds/treap_sequence.hpp:304` `atl::treap_sequence::treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 2 | 1 | 100 |
| `atl/ds/treap_sequence.hpp:432` `atl::treap_sequence::treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | default ctor | 2 | 1 | 77 |
| `atl/ds/treap_sequence.hpp:437` `atl::treap_sequence::treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 2 | 1 | 240 |
| `atl/ds/treap_sequence.hpp:447` `atl::treap_sequence::treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | ctor-explicit | 2 | 1 | 342 |
| `atl/ds/treap_sequence.hpp:454` `atl::treap_sequence::treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 2 | 1 | 446 |
| `atl/ds/treap_sequence.hpp:468` `atl::treap_sequence::treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 1 | 1 | 548 |
| `atl/ds/treap_sequence.hpp:480` `atl::treap_sequence::treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 2 | 1 | 777 |
| `atl/ds/treap_sequence.hpp:495` `atl::treap_sequence::treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | multi-arg ctor | 2 | 1 | 886 |
| `atl/ds/treap_sequence.hpp:502` `atl::treap_sequence::treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | copy/move ctor | 2 | 1 | 105 |
| `atl/ds/treap_sequence.hpp:505` `atl::treap_sequence::treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | copy/move ctor | 2 | 1 | 104 |
| `atl/ds/treap_sequence.hpp:508` `atl::treap_sequence::operator=` | copy/move assignment | 2 | 1 | 205 |
| `atl/ds/treap_sequence.hpp:518` `atl::treap_sequence::operator=` | copy/move assignment | 2 | 1 | 211 |
| `atl/ds/treap_sequence.hpp:529` `atl::treap_sequence::~treap_sequence<T, Op, F, Mapping, Compose, IsReversible>` | destructor | 2 | 1 | 60 |
| `atl/ds/treap_sequence.hpp:826` `atl::<deduction guide for treap_sequence>` | deduction guide | 3 | 1 | 108 |
| `atl/ds/treap_sequence.hpp:829` `atl::<deduction guide for treap_sequence>` | deduction guide | 3 | 1 | 74 |
| `atl/ds/treap_sequence.hpp:832` `atl::<deduction guide for treap_sequence>` | deduction guide | 3 | 1 | 106 |
| `atl/ds/treap_sequence.hpp:839` `atl::<deduction guide for treap_sequence>` | deduction guide | 3 | 1 | 201 |
| `atl/ds/treap_sequence.hpp:842` `atl::<deduction guide for treap_sequence>` | deduction guide | 3 | 1 | 211 |
| `atl/ds/treap_sequence.hpp:847` `atl::<deduction guide for treap_sequence>` | deduction guide | 3 | 1 | 310 |
| `atl/ds/wavelet_matrix.hpp:217` `atl::<deduction guide for wavelet_matrix>` | deduction guide | 2 | 1 | 108 |
| `atl/ds/wavelet_matrix.hpp:80` `atl::wavelet_matrix<unsigned int>::wavelet_matrix` | default ctor | 3 | 1 | 37 |
| `atl/ds/wavelet_matrix.hpp:92` `atl::wavelet_matrix<unsigned int>::wavelet_matrix` | ctor-explicit | 1 | 1 | 1,480 |
| `atl/ds/weighted_dsu.hpp:135` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 1 | 2 | 898 |
| `atl/ds/weighted_dsu.hpp:146` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | default ctor | 2 | 1 | 25 |
| `atl/ds/weighted_dsu.hpp:146` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | default ctor | 3 | 1 | 25 |
| `atl/ds/weighted_dsu.hpp:156` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | ctor-explicit | 1 | 1 | 263 |
| `atl/ds/weighted_dsu.hpp:156` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | ctor-explicit | 3 | 1 | 263 |
| `atl/ds/weighted_dsu.hpp:170` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 1 | 1 | 272 |
| `atl/ds/weighted_dsu.hpp:170` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 2 | 1 | 272 |
| `atl/ds/weighted_dsu.hpp:181` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 2 | 1 | 269 |
| `atl/ds/weighted_dsu.hpp:181` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 3 | 1 | 269 |
| `atl/ds/weighted_dsu.hpp:194` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 2 | 1 | 279 |
| `atl/ds/weighted_dsu.hpp:194` `atl::weighted_dsu::weighted_dsu<Potential, Op, Inverse>` | multi-arg ctor | 3 | 1 | 279 |
| `atl/geometry/angular_sort.hpp:73` `atl::geometry::polar_angle_less::operator()` | operator() | 1 | 3 | 1,062 |
| `atl/geometry/angular_sort.hpp:73` `atl::geometry::polar_angle_less::operator()` | operator() | 3 | 1 | 354 |
| `atl/geometry/angular_sort.hpp:83` `atl::geometry::<deduction guide for polar_angle_less>` | deduction guide | 3 | 4 | 340 |
| `atl/geometry/calipers.hpp:112` `atl::geometry::antipodal_view::antipodal_view<R, Policy>` | ctor-explicit | 1 | 1 | 706 |
| `atl/geometry/calipers.hpp:112` `atl::geometry::antipodal_view::antipodal_view<R, Policy>` | ctor-explicit | 2 | 2 | 1,412 |
| `atl/geometry/calipers.hpp:136` `atl::geometry::antipodal_view::iterator::iterator` | multi-arg ctor | 1 | 1 | 175 |
| `atl/geometry/calipers.hpp:136` `atl::geometry::antipodal_view::iterator::iterator` | multi-arg ctor | 2 | 2 | 350 |
| `atl/geometry/calipers.hpp:150` `atl::geometry::antipodal_view::iterator::iterator` | default ctor | 2 | 3 | 93 |
| `atl/geometry/calipers.hpp:153` `atl::geometry::antipodal_view::iterator::operator*` | operator-arithmetic | 1 | 1 | 183 |
| `atl/geometry/calipers.hpp:153` `atl::geometry::antipodal_view::iterator::operator*` | operator-arithmetic | 3 | 2 | 366 |
| `atl/geometry/calipers.hpp:158` `atl::geometry::antipodal_view::iterator::operator++` | operator-arithmetic | 1 | 1 | 310 |
| `atl/geometry/calipers.hpp:158` `atl::geometry::antipodal_view::iterator::operator++` | operator-arithmetic | 3 | 2 | 620 |
| `atl/geometry/calipers.hpp:170` `atl::geometry::antipodal_view::iterator::operator++` | operator-arithmetic | 3 | 3 | 282 |
| `atl/geometry/calipers.hpp:191` `atl::geometry::<deduction guide for antipodal_view>` | deduction guide | 2 | 1 | 141 |
| `atl/geometry/calipers.hpp:191` `atl::geometry::<deduction guide for antipodal_view>` | deduction guide | 3 | 2 | 282 |
| `atl/geometry/calipers.hpp:200` `atl::geometry::anti_parallel_t::anti_parallel_t` | default ctor | 1 | 3 | 111 |
| `atl/geometry/calipers.hpp:322` `atl::geometry::dual_support_view::dual_support_view<RP, RQ, Policy>` | multi-arg ctor | 1 | 2 | 678 |
| `atl/geometry/calipers.hpp:322` `atl::geometry::dual_support_view::dual_support_view<RP, RQ, Policy>` | multi-arg ctor | 2 | 1 | 339 |
| `atl/geometry/calipers.hpp:330` `atl::geometry::dual_support_view::dual_support_view<RP, RQ, Policy>` | multi-arg ctor | 2 | 2 | 762 |
| `atl/geometry/calipers.hpp:330` `atl::geometry::dual_support_view::dual_support_view<RP, RQ, Policy>` | multi-arg ctor | 3 | 1 | 381 |
| `atl/geometry/calipers.hpp:342` `atl::geometry::dual_support_view::iterator::iterator` | multi-arg ctor | 1 | 2 | 612 |
| `atl/geometry/calipers.hpp:342` `atl::geometry::dual_support_view::iterator::iterator` | multi-arg ctor | 2 | 1 | 306 |
| `atl/geometry/calipers.hpp:373` `atl::geometry::dual_support_view::iterator::iterator` | default ctor | 2 | 3 | 93 |
| `atl/geometry/calipers.hpp:376` `atl::geometry::dual_support_view::iterator::operator*` | operator-arithmetic | 1 | 2 | 508 |
| `atl/geometry/calipers.hpp:376` `atl::geometry::dual_support_view::iterator::operator*` | operator-arithmetic | 3 | 1 | 254 |
| `atl/geometry/calipers.hpp:382` `atl::geometry::dual_support_view::iterator::operator++` | operator-arithmetic | 1 | 2 | 490 |
| `atl/geometry/calipers.hpp:382` `atl::geometry::dual_support_view::iterator::operator++` | operator-arithmetic | 3 | 1 | 245 |
| `atl/geometry/calipers.hpp:394` `atl::geometry::dual_support_view::iterator::operator++` | operator-arithmetic | 3 | 3 | 282 |
| `atl/geometry/calipers.hpp:421` `atl::geometry::<deduction guide for dual_support_view>` | deduction guide | 2 | 2 | 358 |
| `atl/geometry/calipers.hpp:421` `atl::geometry::<deduction guide for dual_support_view>` | deduction guide | 3 | 1 | 179 |
| `atl/geometry/calipers.hpp:425` `atl::geometry::<deduction guide for dual_support_view>` | deduction guide | 3 | 3 | 588 |
| `atl/geometry/calipers.hpp:629` `atl::geometry::detail::rectangle_sweep::rectangle_sweep<T, Policy>` | multi-arg ctor | 3 | 3 | 8,163 |
| `atl/geometry/convex_hull.hpp:26` `atl::geometry::keep_collinear_t::keep_collinear_t` | default ctor | 1 | 4 | 152 |
| `atl/geometry/convex_hull.hpp:398` `atl::geometry::detail::hull_chain::operator[]` | operator-other | 3 | 4 | 484 |
| `atl/geometry/delaunay.hpp:316` `atl::geometry::detail::delaunay_builder::delaunay_builder<T>` | multi-arg ctor | 1 | 1 | 125 |
| `atl/geometry/halfplane_intersection.hpp:419` `atl::geometry::detail::cursor_deque::cursor_deque<Element>` | ctor-explicit | 1 | 1 | 89 |
| `atl/geometry/halfplane_intersection.hpp:434` `atl::geometry::detail::cursor_deque::operator[]` | operator-other | 1 | 1 | 106 |
| `atl/geometry/halfplane_intersection.hpp:950` `atl::geometry::halfplane_sweep_less::operator()` | operator() | 3 | 1 | 398 |
| `atl/geometry/point.hpp:126` `atl::geometry::point::operator+=` | operator-arithmetic | 1 | 7 | 721 |
| `atl/geometry/point.hpp:126` `atl::geometry::point::operator+=` | operator-arithmetic | 3 | 11 | 1,133 |
| `atl/geometry/point.hpp:133` `atl::geometry::point::operator-=` | operator-arithmetic | 1 | 5 | 515 |
| `atl/geometry/point.hpp:133` `atl::geometry::point::operator-=` | operator-arithmetic | 3 | 13 | 1,339 |
| `atl/geometry/point.hpp:140` `atl::geometry::point::operator*=` | operator-arithmetic | 3 | 18 | 1,710 |
| `atl/geometry/point_location.hpp:116` `atl::geometry::detail::slab_frame::slab_frame<T>` | multi-arg ctor | 1 | 1 | 3,200 |
| `atl/geometry/point_location.hpp:385` `atl::geometry::point_location::pool::pool` | default ctor | 1 | 1 | 96 |
| `atl/geometry/point_location.hpp:413` `atl::geometry::point_location::point_location<T>` | multi-arg ctor | 3 | 1 | 164 |
| `atl/geometry/point_location.hpp:419` `atl::geometry::point_location::point_location<T>` | multi-arg ctor | 1 | 1 | 569 |
| `atl/geometry/point_location.hpp:534` `atl::geometry::<deduction guide for point_location>` | deduction guide | 2 | 1 | 119 |
| `atl/geometry/point_location.hpp:603` `atl::geometry::locations_of(const arrangement &, const R &, const Q &)::status_less::operator()` | operator() | 3 | 1 | 103 |
| `atl/geometry/point_location.hpp:607` `atl::geometry::locations_of(const arrangement &, const R &, const Q &)::status_less::operator()` | operator() | 3 | 1 | 103 |
| `atl/geometry/point_location.hpp:611` `atl::geometry::locations_of(const arrangement &, const R &, const Q &)::status_less::operator()` | operator() | 3 | 1 | 103 |
| `atl/geometry/segment_intersections.hpp:129` `atl::geometry::detail::segment_sweep::segment_sweep<T>` | ctor-explicit | 1 | 3 | 723 |
| `atl/geometry/segment_intersections.hpp:129` `atl::geometry::detail::segment_sweep::segment_sweep<T>` | ctor-explicit | 2 | 1 | 241 |
| `atl/geometry/segment_intersections.hpp:137` `atl::geometry::detail::segment_sweep::segment_sweep<T>` | ctor-explicit | 1 | 4 | 4,644 |
| `atl/geometry/segment_intersections.hpp:168` `atl::geometry::detail::segment_sweep::segment_sweep<T>` | copy/move ctor | 2 | 4 | 184 |
| `atl/geometry/segment_intersections.hpp:169` `atl::geometry::detail::segment_sweep::operator=` | copy/move assignment | 2 | 4 | 228 |
| `atl/geometry/segment_intersections.hpp:318` `atl::geometry::detail::segment_sweep::status_less::operator()` | operator() | 1 | 4 | 1,944 |
| `atl/geometry/segment_intersections.hpp:333` `atl::geometry::detail::segment_sweep::status_less::operator()` | operator() | 1 | 4 | 396 |
| `atl/geometry/segment_intersections.hpp:337` `atl::geometry::detail::segment_sweep::status_less::operator()` | operator() | 1 | 4 | 396 |
| `atl/geometry/segment_intersections.hpp:487` `atl::geometry::segment_intersections_view::segment_intersections_view<R>` | ctor-explicit | 1 | 1 | 91 |
| `atl/geometry/segment_intersections.hpp:487` `atl::geometry::segment_intersections_view::segment_intersections_view<R>` | ctor-explicit | 2 | 3 | 273 |
| `atl/geometry/segment_intersections.hpp:501` `atl::geometry::segment_intersections_view::iterator::iterator` | default ctor | 2 | 4 | 84 |
| `atl/geometry/segment_intersections.hpp:504` `atl::geometry::segment_intersections_view::iterator::iterator` | ctor-explicit | 1 | 1 | 86 |
| `atl/geometry/segment_intersections.hpp:504` `atl::geometry::segment_intersections_view::iterator::iterator` | ctor-explicit | 2 | 3 | 258 |
| `atl/geometry/segment_intersections.hpp:509` `atl::geometry::segment_intersections_view::iterator::operator*` | operator-arithmetic | 1 | 1 | 86 |
| `atl/geometry/segment_intersections.hpp:509` `atl::geometry::segment_intersections_view::iterator::operator*` | operator-arithmetic | 3 | 3 | 258 |
| `atl/geometry/segment_intersections.hpp:512` `atl::geometry::segment_intersections_view::iterator::operator++` | operator-arithmetic | 1 | 1 | 93 |
| `atl/geometry/segment_intersections.hpp:512` `atl::geometry::segment_intersections_view::iterator::operator++` | operator-arithmetic | 3 | 3 | 279 |
| `atl/geometry/segment_intersections.hpp:518` `atl::geometry::segment_intersections_view::iterator::operator++` | operator-arithmetic | 3 | 4 | 132 |
| `atl/geometry/segment_intersections.hpp:541` `atl::geometry::<deduction guide for segment_intersections_view>` | deduction guide | 2 | 1 | 93 |
| `atl/geometry/segment_intersections.hpp:541` `atl::geometry::<deduction guide for segment_intersections_view>` | deduction guide | 3 | 3 | 279 |
| `atl/geometry/triangulation.hpp:264` `atl::geometry::detail::monotone_sweep_triangulation(R &&, const Policy)::edge_less::operator()` | operator() | 1 | 1 | 554 |
| `atl/geometry/triangulation.hpp:284` `atl::geometry::detail::monotone_sweep_triangulation(R &&, const Policy)::edge_less::operator()` | operator() | 1 | 1 | 127 |
| `atl/geometry/triangulation.hpp:288` `atl::geometry::detail::monotone_sweep_triangulation(R &&, const Policy)::edge_less::operator()` | operator() | 3 | 1 | 127 |
| `atl/graph/csr.hpp:140` `atl::graph::csr::csr<Edge>` | multi-arg ctor | 1 | 3 | 414 |
| `atl/graph/csr.hpp:140` `atl::graph::csr::csr<Edge>` | multi-arg ctor | 3 | 4 | 552 |
| `atl/graph/csr.hpp:156` `atl::graph::csr::csr<Edge>` | multi-arg ctor | 1 | 4 | 1,668 |
| `atl/graph/csr.hpp:156` `atl::graph::csr::csr<Edge>` | multi-arg ctor | 3 | 3 | 1,251 |
| `atl/graph/csr.hpp:172` `atl::graph::csr::csr<Edge>` | multi-arg ctor | 3 | 7 | 3,234 |
| `atl/graph/csr.hpp:241` `atl::graph::<deduction guide for csr>` | deduction guide | 2 | 4 | 428 |
| `atl/graph/csr.hpp:241` `atl::graph::<deduction guide for csr>` | deduction guide | 3 | 3 | 321 |
| `atl/graph/functional_graph.hpp:182` `atl::graph::detail::functional_graph_topology::functional_graph_topology` | ctor-explicit | 1 | 1 | 399 |
| `atl/graph/functional_graph.hpp:246` `atl::graph::detail::functional_graph_operation::operator()` | operator() | 1 | 1 | 155 |
| `atl/graph/functional_graph.hpp:264` `atl::graph::detail::functional_graph_cycle_folds::functional_graph_cycle_folds<Value, Operation>` | multi-arg ctor | 1 | 1 | 1,243 |
| `atl/graph/functional_graph.hpp:330` `atl::graph::detail::functional_graph_walk_engine::functional_graph_walk_engine<Value, Operation, IsPrefix>` | multi-arg ctor | 1 | 1 | 1,212 |
| `atl/graph/functional_graph.hpp:367` `atl::graph::detail::functional_graph_walk_engine::functional_graph_walk_engine<Value, Operation, IsPrefix>` | multi-arg ctor | 1 | 1 | 288 |
| `atl/graph/functional_graph.hpp:375` `atl::graph::detail::functional_graph_walk_engine::functional_graph_walk_engine<Value, Operation, IsPrefix>` | multi-arg ctor | 1 | 1 | 402 |
| `atl/graph/functional_graph.hpp:446` `atl::graph::functional_graph_successor_index::functional_graph_successor_index` | ctor-explicit | 1 | 1 | 203 |
| `atl/graph/functional_graph.hpp:492` `atl::graph::functional_graph_walk_index::functional_graph_walk_index<Value, Operation>` | multi-arg ctor | 1 | 1 | 355 |
| `atl/graph/functional_graph.hpp:78` `atl::graph::detail::validated_successors_t::validated_successors_t` | default ctor | 1 | 1 | 44 |
| `atl/graph/lca.hpp:342` `atl::graph::lca::lca` | multi-arg ctor | 2 | 5 | 700 |
| `atl/graph/lca.hpp:352` `atl::graph::lca::lca` | ctor-explicit | 2 | 5 | 520 |
| `atl/graph/lca.hpp:362` `atl::graph::lca::lca` | ctor-explicit | 1 | 5 | 3,370 |
| `atl/graph/lca.hpp:384` `atl::graph::lca::operator()` | operator() | 1 | 5 | 3,550 |
| `atl/graph/max_flow.hpp:76` `atl::graph::detail::residual::residual<Flow>` | multi-arg ctor | 1 | 2 | 2,346 |
| `atl/graph/max_flow.hpp:76` `atl::graph::detail::residual::residual<Flow>` | multi-arg ctor | 3 | 2 | 2,346 |
| `atl/graph/min_cost_flow.hpp:102` `atl::graph::detail::cost_residual::cost_residual<Flow, Cost>` | multi-arg ctor | 1 | 1 | 1,316 |
| `atl/graph/partition.hpp:30` `atl::graph::vertex_groups::operator[]` | operator-other | 1 | 2 | 424 |
| `atl/graph/partition.hpp:30` `atl::graph::vertex_groups::operator[]` | operator-other | 3 | 5 | 1,060 |
| `atl/graph/partition.hpp:53` `atl::graph::vertex_partition::vertex_partition` | multi-arg ctor | 1 | 5 | 2,045 |
| `atl/graph/partition.hpp:53` `atl::graph::vertex_partition::vertex_partition` | multi-arg ctor | 3 | 2 | 818 |
| `atl/graph/traversal.hpp:224` `atl::graph::detail::edge_scan::edge_scan<Graph, PrefersIndex>` | default ctor | 1 | 1 | 22 |
| `atl/graph/traversal.hpp:224` `atl::graph::detail::edge_scan::edge_scan<Graph, PrefersIndex>` | default ctor | 3 | 7 | 154 |
| `atl/graph/traversal.hpp:226` `atl::graph::detail::edge_scan::edge_scan<Graph, PrefersIndex>` | multi-arg ctor | 1 | 1 | 157 |
| `atl/graph/traversal.hpp:226` `atl::graph::detail::edge_scan::edge_scan<Graph, PrefersIndex>` | multi-arg ctor | 3 | 7 | 1,099 |
| `atl/graph/traversal.hpp:230` `atl::graph::detail::edge_scan::edge_scan<Graph, PrefersIndex>` | multi-arg ctor | 1 | 7 | 4,074 |
| `atl/graph/traversal.hpp:230` `atl::graph::detail::edge_scan::edge_scan<Graph, PrefersIndex>` | multi-arg ctor | 3 | 1 | 582 |
| `atl/graph/traversal.hpp:415` `atl::graph::bfs_view::iterator::iterator` | default ctor | 3 | 1 | 21 |
| `atl/graph/traversal.hpp:418` `atl::graph::bfs_view::iterator::iterator` | multi-arg ctor | 1 | 1 | 219 |
| `atl/graph/traversal.hpp:425` `atl::graph::bfs_view::iterator::iterator` | multi-arg ctor | 3 | 1 | 223 |
| `atl/graph/traversal.hpp:432` `atl::graph::bfs_view::iterator::operator*` | operator-arithmetic | 1 | 1 | 80 |
| `atl/graph/traversal.hpp:435` `atl::graph::bfs_view::iterator::operator++` | operator-arithmetic | 1 | 1 | 701 |
| `atl/graph/traversal.hpp:462` `atl::graph::bfs_view::iterator::operator++` | operator-arithmetic | 3 | 1 | 43 |
| `atl/graph/traversal.hpp:465` `atl::graph::bfs_view::iterator::operator==` | operator-comparison | 1 | 1 | 91 |
| `atl/graph/traversal.hpp:469` `atl::graph::bfs_view::bfs_view<Graph, Marker>` | multi-arg ctor | 1 | 1 | 154 |
| `atl/graph/traversal.hpp:474` `atl::graph::bfs_view::bfs_view<Graph, Marker>` | multi-arg ctor | 3 | 1 | 195 |
| `atl/graph/traversal.hpp:481` `atl::graph::bfs_view::bfs_view<Graph, Marker>` | multi-arg ctor | 3 | 1 | 236 |
| `atl/graph/traversal.hpp:490` `atl::graph::bfs_view::bfs_view<Graph, Marker>` | multi-arg ctor | 3 | 1 | 277 |
| `atl/graph/traversal.hpp:57` `atl::graph::dense_marker::dense_marker` | ctor-explicit | 1 | 3 | 318 |
| `atl/graph/traversal.hpp:91` `atl::graph::epoch_marker::epoch_marker` | ctor-explicit | 1 | 1 | 64 |
| `atl/graph/traversal.hpp:945` `atl::graph::detail::fold_no_finalize::operator()` | operator() | 1 | 1 | 87 |
| `atl/graph/traversal.hpp:945` `atl::graph::detail::fold_no_finalize::operator()` | operator() | 3 | 2 | 174 |
| `atl/io/reader.hpp:249` `atl::io::reader::reader` | default ctor | 1 | 1 | 169 |
| `atl/io/reader.hpp:279` `atl::io::reader::reader` | copy/move ctor | 3 | 1 | 32 |
| `atl/io/reader.hpp:281` `atl::io::reader::operator=` | copy/move assignment | 3 | 1 | 43 |
| `atl/io/reader.hpp:284` `atl::io::reader::~reader` | destructor | 2 | 1 | 56 |
| `atl/io/reader.hpp:434` `atl::io::reader::operator>>` | operator-arithmetic | 3 | 1 | 97 |
| `atl/io/writer.hpp:650` `atl::io::writer::writer` | default ctor | 1 | 1 | 145 |
| `atl/io/writer.hpp:653` `atl::io::writer::writer` | copy/move ctor | 3 | 1 | 32 |
| `atl/io/writer.hpp:655` `atl::io::writer::operator=` | copy/move assignment | 3 | 1 | 43 |
| `atl/io/writer.hpp:658` `atl::io::writer::~writer` | destructor | 2 | 1 | 48 |
| `atl/io/writer.hpp:840` `atl::io::writer::operator<<` | operator-arithmetic | 3 | 1 | 161 |
| `atl/nt/bit_matrix.hpp:35` `atl::bit_matrix::bit_matrix` | default ctor | 1 | 1 | 32 |
| `atl/nt/bit_matrix.hpp:41` `atl::bit_matrix::bit_matrix` | multi-arg ctor | 1 | 1 | 246 |
| `atl/nt/bitwise_convolution.hpp:102` `atl::detail::subset_mobius_butterfly::operator()` | operator() | 1 | 1 | 96 |
| `atl/nt/bitwise_convolution.hpp:110` `atl::detail::superset_zeta_butterfly::operator()` | operator() | 1 | 1 | 96 |
| `atl/nt/bitwise_convolution.hpp:118` `atl::detail::superset_mobius_butterfly::operator()` | operator() | 1 | 1 | 96 |
| `atl/nt/bitwise_convolution.hpp:126` `atl::detail::walsh_hadamard_butterfly::operator()` | operator() | 1 | 1 | 147 |
| `atl/nt/bitwise_convolution.hpp:94` `atl::detail::subset_zeta_butterfly::operator()` | operator() | 1 | 1 | 96 |
| `atl/nt/crt.hpp:37` `atl::congruence::operator==` | operator-comparison | 3 | 2 | 152 |
| `atl/nt/detail/online_convolution.hpp:183` `atl::detail::online_product_state::online_product_state<Coefficient, Engine, Algorithm, IsFixed>` | default ctor | 1 | 1 | 202 |
| `atl/nt/detail/online_convolution.hpp:183` `atl::detail::online_product_state::online_product_state<Coefficient, Engine, Algorithm, IsFixed>` | default ctor | 3 | 1 | 202 |
| `atl/nt/detail/online_convolution.hpp:194` `atl::detail::online_product_state::online_product_state<Coefficient, Engine, Algorithm, IsFixed>` | ctor-explicit | 1 | 1 | 866 |
| `atl/nt/detail/online_convolution.hpp:194` `atl::detail::online_product_state::online_product_state<Coefficient, Engine, Algorithm, IsFixed>` | ctor-explicit | 3 | 1 | 866 |
| `atl/nt/detail/polynomial_points.hpp:499` `atl::detail::points_tree_storage::points_tree_storage<Coefficient, Engine>` | ctor-explicit | 1 | 1 | 545 |
| `atl/nt/discrete_log.hpp:201` `atl::detail::bsgs_table::bsgs_table<Engine>` | multi-arg ctor | 1 | 1 | 492 |
| `atl/nt/discrete_log.hpp:81` `atl::detail::barrett_multiplier::barrett_multiplier<Reducer>` | ctor-explicit | 1 | 1 | 81 |
| `atl/nt/dynamic_mod_int.hpp:101` `atl::dynamic_mod_int::dynamic_mod_int<T, Id>` | ctor-implicit-converting | 1 | 1 | 387 |
| `atl/nt/dynamic_mod_int.hpp:134` `atl::dynamic_mod_int::operator+=` | operator-arithmetic | 3 | 1 | 139 |
| `atl/nt/dynamic_mod_int.hpp:143` `atl::dynamic_mod_int::operator-=` | operator-arithmetic | 3 | 1 | 139 |
| `atl/nt/dynamic_mod_int.hpp:152` `atl::dynamic_mod_int::operator*=` | operator-arithmetic | 1 | 1 | 124 |
| `atl/nt/dynamic_mod_int.hpp:162` `atl::dynamic_mod_int::operator/=` | operator-arithmetic | 3 | 1 | 95 |
| `atl/nt/dynamic_mod_int.hpp:247` `atl::dynamic_mod_int::operator+` | operator-arithmetic | 3 | 1 | 61 |
| `atl/nt/dynamic_mod_int.hpp:253` `atl::dynamic_mod_int::operator-` | operator-arithmetic | 3 | 1 | 101 |
| `atl/nt/dynamic_mod_int.hpp:260` `atl::dynamic_mod_int::operator==` | operator-comparison | 3 | 1 | 92 |
| `atl/nt/dynamic_mod_int.hpp:266` `atl::dynamic_mod_int::operator++` | operator-arithmetic | 3 | 1 | 92 |
| `atl/nt/dynamic_mod_int.hpp:275` `atl::dynamic_mod_int::operator++` | operator-arithmetic | 3 | 1 | 100 |
| `atl/nt/dynamic_mod_int.hpp:285` `atl::dynamic_mod_int::operator--` | operator-arithmetic | 3 | 1 | 92 |
| `atl/nt/dynamic_mod_int.hpp:294` `atl::dynamic_mod_int::operator--` | operator-arithmetic | 3 | 1 | 100 |
| `atl/nt/dynamic_mod_int.hpp:92` `atl::dynamic_mod_int::dynamic_mod_int<T, Id>` | default ctor | 3 | 1 | 28 |
| `atl/nt/factorial_table.hpp:32` `atl::factorial_table::factorial_table<Coefficient>` | ctor-explicit | 1 | 4 | 352 |
| `atl/nt/matrix.hpp:264` `atl::matrix::matrix<T, Semiring>` | default ctor | 2 | 1 | 29 |
| `atl/nt/matrix.hpp:264` `atl::matrix<atl::mod_int<unsigned int, 998244353>>::matrix` | default ctor | 1 | 1 | 29 |
| `atl/nt/matrix.hpp:270` `atl::matrix::matrix<T, Semiring>` | multi-arg ctor | 2 | 1 | 106 |
| `atl/nt/matrix.hpp:270` `atl::matrix<atl::mod_int<unsigned int, 998244353>>::matrix` | multi-arg ctor | 1 | 1 | 106 |
| `atl/nt/matrix.hpp:277` `atl::matrix::matrix<T, Semiring>` | multi-arg ctor | 2 | 1 | 204 |
| `atl/nt/matrix.hpp:277` `atl::matrix<atl::mod_int<unsigned int, 998244353>>::matrix` | multi-arg ctor | 1 | 1 | 204 |
| `atl/nt/matrix.hpp:285` `atl::matrix::matrix<T, Semiring>` | ctor-implicit-converting | 2 | 1 | 397 |
| `atl/nt/matrix.hpp:285` `atl::matrix<atl::mod_int<unsigned int, 998244353>>::matrix` | ctor-implicit-converting | 1 | 1 | 397 |
| `atl/nt/matrix.hpp:314` `atl::matrix::operator()` | operator() | 3 | 1 | 142 |
| `atl/nt/matrix.hpp:314` `atl::matrix<atl::mod_int<unsigned int, 998244353>>::operator()` | operator() | 1 | 1 | 142 |
| `atl/nt/matrix.hpp:320` `atl::matrix::operator()` | operator() | 3 | 1 | 154 |
| `atl/nt/matrix.hpp:320` `atl::matrix<atl::mod_int<unsigned int, 998244353>>::operator()` | operator() | 1 | 1 | 154 |
| `atl/nt/matrix.hpp:344` `atl::matrix::operator+=` | operator-arithmetic | 3 | 1 | 252 |
| `atl/nt/matrix.hpp:344` `atl::matrix<atl::mod_int<unsigned int, 998244353>>::operator+=` | operator-arithmetic | 3 | 1 | 252 |
| `atl/nt/matrix.hpp:388` `atl::matrix::operator*=` | operator-arithmetic | 3 | 1 | 79 |
| `atl/nt/matrix.hpp:388` `atl::matrix<atl::mod_int<unsigned int, 998244353>>::operator*=` | operator-arithmetic | 3 | 1 | 79 |
| `atl/nt/mod_int.hpp:176` `atl::mod_int::operator+` | operator-arithmetic | 3 | 19 | 1,102 |
| `atl/nt/mod_int.hpp:181` `atl::mod_int::operator-` | operator-arithmetic | 1 | 11 | 946 |
| `atl/nt/mod_int.hpp:181` `atl::mod_int::operator-` | operator-arithmetic | 2 | 6 | 516 |
| `atl/nt/mod_int.hpp:181` `atl::mod_int::operator-` | operator-arithmetic | 3 | 2 | 172 |
| `atl/nt/mod_int.hpp:186` `atl::mod_int::operator==` | operator-comparison | 1 | 14 | 1,246 |
| `atl/nt/mod_int.hpp:186` `atl::mod_int::operator==` | operator-comparison | 2 | 3 | 267 |
| `atl/nt/mod_int.hpp:186` `atl::mod_int::operator==` | operator-comparison | 3 | 2 | 178 |
| `atl/nt/mod_int.hpp:191` `atl::mod_int::operator++` | operator-arithmetic | 3 | 19 | 1,691 |
| `atl/nt/mod_int.hpp:197` `atl::mod_int::operator++` | operator-arithmetic | 3 | 19 | 1,881 |
| `atl/nt/mod_int.hpp:204` `atl::mod_int::operator--` | operator-arithmetic | 3 | 19 | 1,691 |
| `atl/nt/mod_int.hpp:210` `atl::mod_int::operator--` | operator-arithmetic | 3 | 19 | 1,881 |
| `atl/nt/mod_int.hpp:49` `atl::mod_int::mod_int<T, Mod>` | default ctor | 1 | 17 | 629 |
| `atl/nt/mod_int.hpp:49` `atl::mod_int::mod_int<T, Mod>` | default ctor | 3 | 2 | 74 |
| `atl/nt/mod_int.hpp:57` `atl::mod_int::mod_int<T, Mod>` | ctor-implicit-converting | 1 | 19 | 2,109 |
| `atl/nt/mod_int.hpp:76` `atl::mod_int::operator+=` | operator-arithmetic | 1 | 16 | 1,888 |
| `atl/nt/mod_int.hpp:76` `atl::mod_int::operator+=` | operator-arithmetic | 2 | 1 | 118 |
| `atl/nt/mod_int.hpp:76` `atl::mod_int::operator+=` | operator-arithmetic | 3 | 2 | 236 |
| `atl/nt/mod_int.hpp:82` `atl::mod_int::operator-=` | operator-arithmetic | 1 | 11 | 1,298 |
| `atl/nt/mod_int.hpp:82` `atl::mod_int::operator-=` | operator-arithmetic | 2 | 5 | 590 |
| `atl/nt/mod_int.hpp:82` `atl::mod_int::operator-=` | operator-arithmetic | 3 | 3 | 354 |
| `atl/nt/mod_int.hpp:88` `atl::mod_int::operator*=` | operator-arithmetic | 1 | 17 | 2,006 |
| `atl/nt/mod_int.hpp:88` `atl::mod_int::operator*=` | operator-arithmetic | 2 | 1 | 118 |
| `atl/nt/mod_int.hpp:88` `atl::mod_int::operator*=` | operator-arithmetic | 3 | 1 | 118 |
| `atl/nt/mod_int.hpp:97` `atl::mod_int::operator/=` | operator-arithmetic | 1 | 10 | 890 |
| `atl/nt/mod_int.hpp:97` `atl::mod_int::operator/=` | operator-arithmetic | 2 | 5 | 445 |
| `atl/nt/mod_int.hpp:97` `atl::mod_int::operator/=` | operator-arithmetic | 3 | 4 | 356 |
| `atl/nt/modular.hpp:117` `atl::barrett::barrett` | default ctor | 3 | 3 | 90 |
| `atl/nt/modular.hpp:123` `atl::barrett::barrett` | ctor-explicit | 1 | 2 | 214 |
| `atl/nt/modular.hpp:123` `atl::barrett::barrett` | ctor-explicit | 3 | 1 | 107 |
| `atl/nt/modular.hpp:176` `atl::barrett64::barrett64` | default ctor | 1 | 1 | 31 |
| `atl/nt/modular.hpp:176` `atl::barrett64::barrett64` | default ctor | 3 | 21 | 672 |
| `atl/nt/modular.hpp:182` `atl::barrett64::barrett64` | ctor-explicit | 1 | 22 | 2,728 |
| `atl/nt/montgomery.hpp:260` `atl::detail::montgomery64::montgomery64` | ctor-explicit | 1 | 14 | 3,794 |
| `atl/nt/montgomery.hpp:260` `atl::detail::montgomery64::montgomery64` | ctor-explicit | 3 | 2 | 542 |
| `atl/nt/ntt_engine.hpp:190` `atl::scalar_ntt::twiddles::twiddles<Z>` | default ctor | 1 | 11 | 7,370 |
| `atl/nt/ntt_engine.hpp:91` `atl::ntt_engine_base::ntt_engine_base<Derived>` | default ctor | 3 | 11 | 308 |
| `atl/nt/online_convolution.hpp:83` `atl::online_fixed_convolution::online_fixed_convolution<Coefficient, Engine>` | ctor-explicit | 1 | 1 | 189 |
| `atl/nt/polynomial_points.hpp:93` `atl::polynomial_point_tree::polynomial_point_tree<Coefficient, Engine>` | ctor-explicit | 1 | 1 | 212 |
| `atl/nt/sieve.hpp:178` `atl::sieve::sieve` | ctor-explicit | 1 | 1 | 160 |
| `atl/nt/sieve.hpp:26` `atl::sieve_item_info::operator==` | operator-comparison | 3 | 16 | 1,296 |
| `atl/nt/sieve.hpp:37` `atl::prime_factor::operator==` | operator-comparison | 3 | 16 | 1,248 |
| `atl/strings/aho_corasick.hpp:169` `atl::aho_corasick::aho_corasick<Alphabet>` | ctor-explicit | 1 | 1 | 1,568 |
| `atl/strings/aho_corasick.hpp:220` `atl::aho_corasick::aho_corasick<Alphabet>` | default ctor | 2 | 1 | 66 |
| `atl/strings/comparable_text.hpp:144` `atl::index_interval::operator==` | operator-comparison | 3 | 2 | 160 |
| `atl/strings/dense_transitions.hpp:103` `atl::dense_transitions::dense_transitions` | multi-arg ctor | 3 | 1 | 418 |
| `atl/strings/dense_transitions.hpp:114` `atl::dense_transitions::operator()` | operator() | 1 | 1 | 197 |
| `atl/strings/dense_transitions.hpp:54` `atl::dense_transitions::dense_transitions` | multi-arg ctor | 1 | 1 | 511 |
| `atl/strings/dense_transitions.hpp:68` `atl::dense_transitions::dense_transitions` | default ctor | 3 | 1 | 40 |
| `atl/strings/dense_transitions.hpp:71` `atl::dense_transitions::dense_transitions` | copy/move ctor | 1 | 1 | 203 |
| `atl/strings/dense_transitions.hpp:75` `atl::dense_transitions::operator=` | copy/move assignment | 3 | 1 | 150 |
| `atl/strings/dense_transitions.hpp:81` `atl::dense_transitions::dense_transitions` | copy/move ctor | 3 | 1 | 283 |
| `atl/strings/dense_transitions.hpp:86` `atl::dense_transitions::operator=` | copy/move assignment | 1 | 1 | 360 |
| `atl/strings/palindrome_lengths.hpp:41` `atl::palindrome_lengths::palindrome_lengths` | default ctor | 3 | 1 | 31 |
| `atl/strings/palindrome_lengths.hpp:54` `atl::palindrome_lengths::palindrome_lengths` | ctor-explicit | 1 | 1 | 916 |
| `atl/strings/palindromic_tree.hpp:106` `atl::palindromic_tree::palindromic_tree<Alphabet>` | default ctor | 1 | 1 | 149 |
| `atl/strings/sorted_dictionary.hpp:111` `atl::sorted_dictionary::sorted_dictionary<Symbol>` | ctor-explicit | 1 | 1 | 1,641 |
| `atl/strings/sorted_dictionary.hpp:147` `atl::sorted_dictionary::sorted_dictionary<Symbol>` | copy/move ctor | 3 | 1 | 54 |
| `atl/strings/sorted_dictionary.hpp:149` `atl::sorted_dictionary::operator=` | copy/move assignment | 3 | 1 | 65 |
| `atl/strings/sorted_dictionary.hpp:151` `atl::sorted_dictionary::sorted_dictionary<Symbol>` | copy/move ctor | 3 | 1 | 60 |
| `atl/strings/sorted_dictionary.hpp:153` `atl::sorted_dictionary::operator=` | copy/move assignment | 3 | 1 | 71 |
| `atl/strings/sorted_dictionary.hpp:280` `atl::<deduction guide for sorted_dictionary>` | deduction guide | 2 | 1 | 175 |
| `atl/strings/static_trie.hpp:125` `atl::static_trie::static_trie<Alphabet>` | default ctor | 2 | 1 | 112 |
| `atl/strings/static_trie.hpp:140` `atl::static_trie::static_trie<Alphabet>` | ctor-explicit | 1 | 1 | 4,573 |
| `atl/strings/string_hash.hpp:109` `atl::string_hash::string_hash` | ctor-explicit | 1 | 1 | 516 |
| `atl/strings/string_hash.hpp:92` `atl::string_hash::string_hash` | default ctor | 1 | 1 | 138 |
| `atl/strings/suffix_array.hpp:156` `atl::suffix_array::suffix_array` | default ctor | 3 | 2 | 70 |
| `atl/strings/suffix_array.hpp:168` `atl::suffix_array::suffix_array` | ctor-explicit | 1 | 2 | 3,332 |
| `atl/strings/suffix_automaton.hpp:167` `atl::suffix_automaton::suffix_automaton<Alphabet>` | default ctor | 1 | 1 | 45 |
| `atl/strings/suffix_tree.hpp:177` `atl::suffix_tree::suffix_tree<Alphabet>` | default ctor | 1 | 1 | 69 |
| `atl/strings/trie.hpp:118` `atl::trie::trie<Alphabet>` | default ctor | 1 | 1 | 23 |
| `atl/strings/trie.hpp:118` `atl::trie::trie<Alphabet>` | default ctor | 3 | 1 | 23 |
| `atl/types/algebraic.hpp:139` `atl::commutative_tags_t::commutative_tags_t` | default ctor | 1 | 28 | 1,120 |
| `atl/types/algebraic.hpp:164` `atl::commutative_fold_t::commutative_fold_t` | default ctor | 1 | 28 | 1,120 |
| `atl/types/detail/int256.hpp:50` `atl::detail::int256::operator-` | operator-arithmetic | 1 | 2 | 476 |
| `atl/types/detail/int256.hpp:50` `atl::detail::int256::operator-` | operator-arithmetic | 3 | 4 | 952 |
| `atl/types/detail/int256.hpp:65` `atl::detail::int256::operator+=` | operator-arithmetic | 1 | 6 | 1,542 |
| `atl/types/detail/int256.hpp:79` `atl::detail::int256::operator-=` | operator-arithmetic | 1 | 6 | 1,596 |
| `atl/types/tags.hpp:15` `atl::sorted_unique_t::sorted_unique_t` | default ctor | 1 | 4 | 148 |
| `atl/utils/hash.hpp:147` `atl::identity_hash::operator()` | operator() | 3 | 14 | 1,974 |
| `atl/utils/hash.hpp:206` `atl::hash<int>::operator()` | operator() | 1 | 1 | 391 |
| `atl/utils/hash.hpp:206` `atl::hash<type-parameter-0-0>::operator()` | operator() | 3 | 4 | 1,564 |
| `atl/utils/hash.hpp:206` `atl::hash<unsigned int>::operator()` | operator() | 1 | 1 | 391 |
| `atl/utils/hash.hpp:206` `atl::hash<unsigned long>::operator()` | operator() | 1 | 9 | 3,519 |
| `atl/utils/hash.hpp:235` `atl::hash<type-parameter-0-0>::operator()` | operator() | 3 | 15 | 2,325 |
| `atl/utils/hash.hpp:301` `atl::hash<type-parameter-0-0>::operator()` | operator() | 3 | 15 | 11,745 |
| `atl/utils/hash.hpp:338` `atl::hash<std::basic_string_view<char>>::operator()` | operator() | 1 | 15 | 2,400 |
| `atl/utils/hash.hpp:355` `atl::hash<std::basic_string<char>>::operator()` | operator() | 1 | 2 | 194 |
| `atl/utils/hash.hpp:355` `atl::hash<std::basic_string<char>>::operator()` | operator() | 3 | 13 | 1,261 |
| `atl/utils/hash.hpp:367` `atl::hash<type-parameter-0-0 *>::operator()` | operator() | 3 | 15 | 2,580 |
| `atl/utils/hash.hpp:404` `atl::hash<std::pair<type-parameter-0-0, type-parameter-0-1>>::operator()` | operator() | 3 | 13 | 6,695 |
| `atl/utils/hash.hpp:404` `atl::hash<std::pair<unsigned int, std::basic_string<char>>>::operator()` | operator() | 1 | 1 | 515 |
| `atl/utils/hash.hpp:404` `atl::hash<std::pair<unsigned int, unsigned int>>::operator()` | operator() | 1 | 1 | 515 |
| `atl/utils/hash.hpp:429` `atl::hash<std::tuple<type-parameter-0-0...>>::operator()` | operator() | 3 | 15 | 4,155 |
| `atl/utils/hash.hpp:451` `atl::hash<std::vector<type-parameter-0-0, std::allocator<type-parameter-0-0>>>::operator()` | operator() | 3 | 15 | 6,315 |
| `atl/utils/hash.hpp:472` `atl::hash<std::array<type-parameter-0-0, value-parameter-0-1>>::operator()` | operator() | 3 | 15 | 6,135 |

### Conservative trait-evidence bound

Group (2) contains 91 declarations / 16,309 bytes supported only by the conservative record-level trait fallback, without a resolved/unevaluated function reference or original compiler used/referenced flag. Exact overload-selection replay could move some of these bytes to (3); the (3) totals are lower-bound worker candidate pools.

| Kind | Trait-fallback-only declarations | Owned bytes |
| --- | ---: | ---: |
| ctor-explicit | 17 | 3,879 |
| ctor-implicit-converting | 2 | 506 |
| multi-arg ctor | 24 | 7,651 |
| default ctor | 32 | 2,651 |
| copy/move ctor | 8 | 582 |
| copy/move assignment | 8 | 1,040 |

The diagnostic records compiler-exposed CTAD/trait evidence plus a conservative record-level fallback for relevant trait kinds; it does not reproduce the preceding analysis’s full Sema overload-selection replay. Consequently group (2) can include additional overloads of a trait-tested record, and group (3) is a conservative candidate inventory rather than a claim that no other target/substitution could inspect the declaration. R-other likewise retains opaque/mixed bindings rather than guessing a narrower class. No trimming assumption above was adopted.

Finalized 2026-11-22 20:55 UTC; no commit or push.

Diagnostic patch SHA-256: `5ae22d58cdee88024ba4d66f66d6167c96f45f06bc8c5b0b65ad23301cf6b6dc`. `git diff --check` and patch-application checks pass; the SDK is absent.
