<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Caide versus distill: cross-judge byte-gap analysis

2026-11-24. Analysis only, worktree base `6205039` (D3f included), read-only CI run **45372149287**, ATL `5b14f2d`, caide fork `a8b990b`, judge `cses`. No production changes or C++ builds. No commit or push: the opening instruction prohibited committing, despite the contradictory closing instruction.

**Complete: 100/100 selected examples reconciled byte for byte.** Distill-nlm exceeds caide verify by **739,480 B**; compact reduces this to **467,607 B**. Most of the gap is conservative declaration retention, especially names shared across scopes and the member/library inventory. Unknown user conditionals contribute **zero** retained distill bytes in this population. The premise has three exceptions: caide is larger in three DP examples.

## Population and artifact totals

Select exactly `verify.fallback=no && verify.run_check=ok` in the supplied `summary.tsv`: **100 of 126** examples. All **11** repaired examples remain included. Compare **verify**, never noverify, with the actual written distill artifacts. Distill's own report says local verification with runtime not requested; the sweep's separate compile/sample checks say `ok`. Those facts certify the worker run, not another judge.

| Artifact | Actual bytes | Gap versus caide verify |
| --- | ---: | ---: |
| caide verify | 1,692,146 | 0 |
| distill-nlm | 2,431,626 | 739,480 |
| distill-compact | 2,159,753 | 467,607 |

Production compaction saves **271,873 B** on distill's output. This includes whitespace inside extra declarations, so it must not be added to the ownership-based formatting row below. Independent compaction reproduction equals every supplied compact artifact byte for byte.

### Top 20 nlm gaps

| Example | caide verify B | distill-nlm B | nlm gap B | compact B | compact gap B |
| --- | ---: | ---: | ---: | ---: | ---: |
| top-tree-basic | 84,589 | 124,169 | 39,580 | 110,519 | 25,930 |
| subtree-link-cut-tree-basic | 56,072 | 89,887 | 33,815 | 79,767 | 23,695 |
| delaunay-quick | 41,249 | 70,810 | 29,561 | 62,229 | 20,980 |
| weighted-dsu-potentials | 17,686 | 42,648 | 24,962 | 38,097 | 20,411 |
| euler-tour-tree-basic | 39,417 | 62,654 | 23,237 | 56,402 | 16,985 |
| matrix-quick | 29,273 | 51,654 | 22,381 | 46,405 | 17,132 |
| modular-arithmetic-residues | 18,360 | 40,269 | 21,909 | 36,000 | 17,640 |
| time-divide-and-conquer-connectivity | 29,617 | 50,397 | 20,780 | 45,498 | 15,881 |
| persistent-treap-sequence-basic | 17,380 | 38,118 | 20,738 | 33,920 | 16,540 |
| flat-hash-basic | 16,795 | 36,832 | 20,037 | 33,436 | 16,641 |
| power-monoid | 10,495 | 29,929 | 19,434 | 26,758 | 16,263 |
| bipartite-matching-dag | 21,137 | 39,772 | 18,635 | 34,885 | 13,748 |
| persistent-vector-basic | 23,090 | 41,243 | 18,153 | 36,714 | 13,624 |
| weighted-dsu-matrix | 18,543 | 36,378 | 17,835 | 32,471 | 13,928 |
| treap-map-quick-start | 19,393 | 33,650 | 14,257 | 29,934 | 10,541 |
| functional-graphs-quick | 34,025 | 47,858 | 13,833 | 42,356 | 8,331 |
| circle-quick | 43,123 | 55,756 | 12,633 | 49,780 | 6,657 |
| persistent-queue-deque-basic | 15,280 | 27,900 | 12,620 | 24,819 | 9,539 |
| treap-sequence-quick-start | 20,740 | 32,272 | 11,532 | 28,888 | 8,148 |
| lca-path-sum | 16,881 | 27,589 | 10,708 | 24,004 | 7,123 |

Caide is larger than nlm in **3/100**:

| Example | caide B | nlm B | nlm − caide B | compact B |
| --- | ---: | ---: | ---: | ---: |
| dp-optimization-interval-merge | 12,302 | 10,742 | -1,560 | 9,528 |
| dp-optimization-partition | 18,008 | 16,570 | -1,438 | 14,672 |
| dp-optimization-quick | 10,487 | 8,910 | -1,577 | 7,784 |

Caide exceeds compact in 11 examples: assignment-quick (410 B), counting-sort-stable-records (127), dp-optimization-interval-merge (2,774), dp-optimization-partition (3,336), dp-optimization-quick (2,703), dp-optimization-row-minima (97), exact-count-optimum-quick (620), lexicographic-order-quick (153), lyndon-quick (167), max-flow-bounds (1,059), max-flow-quick (538).

## Matching and byte ownership

The method follows [the free-function analysis](2026-11-24-free-function-analysis.md): count written declaration **occurrences across examples**, aggregate the same original declaration only for presentation, and distinguish an immediate retaining cause from a minimal semantic proof. Report source-byte estimates are not output-byte measurements.

1. Parse reports and the two actual outputs. Collapse caide's wrapper/pattern duplicates (`FunctionTemplate`/`Function`, `ClassTemplate`/`CXXRecord`, etc.) at the same qualified name, normalized kind and source line. Collapse distill's implicit-instance rows at the same original file/line/name. Strip template-argument renderings from qualified names for joining, keeping source lines to distinguish specializations and overloads. Infer distill kinds from written C++ syntax; distinguish methods, functions, records, variables, aliases, concepts, enums, guides and assertions. Never count template parameters or ordinary function locals as separate (a) declarations.
2. Caide locations are **`caide-tmp/inlined.cpp` lines**, not original header lines. There is no original ATL checkout or immutable caide inline buffer in the supplied artifact. Align same-name occurrence lists in source order, using kept **and removed** report rows; their original-header/inline line pairs define the coordinate translation. User declaration order is preserved; system includes are handled separately. Verify kind at the resulting inline location. Unique-name anchors also establish header offsets. This is a report-derived coordinate reconstruction, not an independent original-source map or USR match.
3. Of **8,791** mapped written distill report occurrences, **8,790** join a caide report identity: 8,784 by occurrence/line anchors and 5 namespace assertions plus the wavelet definition by header-offset anchors. The one ambiguous 52-byte `wavelet_matrix` forward declaration has three kept caide report locations but only two written caide declarations; match the definition by its anchored line and body shape, leave the forward identity unmatched. Both outputs contain identical forward text. Six euler-tour guides and eight namespace using declarations lack a convenient syntax/report-name join; their unchanged nonwhitespace text also cancels exactly. None is silently declared removed.
4. Obtain declaration extents with tree-sitter C++ (**0.23.4**, runtime **0.27.0**), not a semantic compiler. Grammar-only views replace `if [!]consteval`, select the GCC side of conditional `no_unique_address` attributes, and disambiguate parenthesized `min`/`max` declarators, preserving every byte offset. All 200 views parse without errors. Measurements always slice the **original artifact bytes**, never those syntax views.
5. Directives own complete physical/logical directive lines first. Otherwise the smallest separately reported distill declaration owns its span, including template headers, attributes, terminators and internal whitespace. A structural record excludes its separately reported D3e members. Data, aliases, nested records and hidden friends without separate distill family rows stay with the structural owner. Thus a removed class and its removed members cannot charge the same byte twice. External indentation, separators and blank gaps remain structural trivia.
6. (a) owns those distill spans whose matched caide declarations are removed; follow `family aggregation` to the actual immediate retaining reason, but do not charge dependencies to their ultimate root. For declarations both retain, carve out complete structural subdeclarations caide actually deletes into (d), then compare the residuals. They differ only by caide's dangling `[[nodiscard]]` attributes; no unexplained partial-body rewrite remains. Countervailing caide-only declarations are a separate negative offset, never mislabelled whitespace. Structural subdeclarations in (d) are matched within their enclosing retained owner by normalized name/kind and caide inline source order; assertions and non-function friends are additionally checked against the paired owner's actual tokens, since they have no named distill family row.

The ledger is **signed**: extra distill bytes are positive; extra caide bytes are negative. Each source byte is owned once on its own side; common text cancels. Every example and the aggregate reconcile exactly. The remaining unpaired nonwhitespace declaration text cancels per example after namespace shells are removed: the **4,075 B** net structural remainder is namespace spelling/braces, not an unidentified declaration saving. The report-only join cannot reconstruct complete dependency graphs or certify the absence of alternative retaining reasons.

## Disjoint nlm gap ledger

| Partition | Occurrences | Net bytes |
| --- | ---: | ---: |
| (a) distill-kept / caide-removed declarations | 2,905 | 544,160 |
| (b) directives, includes and branch text | see below | 97,309 |
| (c) whitespace and other structural text | see below | 90,649 |
| (d) caide removals within shared declarations | 344 | 26,998 |
| Countervailing caide-only declarations | 83 | −19,636 |
| **Total nlm − verify** | | **739,480** |

### (a) Immediate retaining reasons

| Distill group | Written occurrences | Owned nlm B |
| --- | ---: | ---: |
| name rule | 1,757 | 342,626 |
| association | 0 | 0 |
| AST | 64 | 13,611 |
| dependent name | 1 | 84 |
| A6/ADL | 0 | 0 |
| A12 library-spelled member | 607 | 105,222 |
| D3e rule 1 kinds | 281 | 57,531 |
| never-removed operator | 20 | 6,582 |
| roots/effects | 168 | 17,895 |
| pinned | 7 | 609 |
| other | 0 | 0 |

The name row comprises **699 methods / 156,977 B**, **152 free functions / 81,741 B**, **510 records / 64,231 B**, **163 variables / 18,761 B**, **92 concepts / 13,810 B**, **133 aliases / 6,323 B**, and **8 enums / 783 B**. Names in retained uninstantiated bodies count. For example, hidden `mod_int` stream operators keep `read_stream_magnitude` and `write_uint128` by actual spelling even when the worker never uses streaming for that instantiation. D3f already removes eligible *unspelled* namespace functions; it cannot delete these helpers while leaving their naming bodies intact.

Rule 1 here is **64 constructor declarations / 19,671 B** and **217 operator declarations / 37,860 B**; no measured destructor/conversion/virtual subpool. A non-member operator inside an indivisible structural record can instead make the **record's** reason `never removed: operator`: that row's 6,582 B is structural ownership, not 6,582 B of standalone operator bodies. Roots/effects split into **90 roots / 5,830 B** and **78 possible-instantiation-effect declarations / 12,065 B**. Namespace assertions account for 7 of those roots / 623 B.

### Top declarations in every nonempty group

Each table groups the same qualified name + kind + original file/line across examples; `N` counts occurrences. Locations come from distill's original provenance. Template-argument spellings are elided in names, never used to merge different source lines. At most 15 rows per group; smaller groups list every distinct source declaration. The zero groups above have no top declarations.

#### name rule

| Declaration / original location | Kind | N | nlm B | Example witness |
| --- | --- | ---: | ---: | --- |
| `atl::detail::read_stream_magnitude` — `atl/nt/detail/uint128_stream.hpp:103` | Function | 5 | 12,125 | matrix-quick |
| `atl::detail::write_uint128` — `atl/nt/detail/uint128_stream.hpp:17` | Function | 4 | 11,516 | matrix-quick |
| `atl::traits::is_unsigned_integral` — `atl/types/traits/integral.hpp:30` | Record | 34 | 9,894 | aho-corasick-quick |
| `atl::graph::lca::build` — `atl/graph/lca.hpp:218` | Method | 5 | 8,415 | functional-graphs-quick |
| `atl::traits::is_signed_integral` — `atl/types/traits/integral.hpp:15` | Record | 29 | 8,004 | aho-corasick-quick |
| `atl::detail::bucket_by_key` — `atl/utils/counting_sort.hpp:67` | Function | 6 | 4,668 | bipartite-matching-dag |
| `atl::traits::detail::extended_integer` — `atl/types/traits/detail/extended_integer.hpp:10` | Record | 33 | 4,587 | aho-corasick-quick |
| `atl::detail::hash_float_value` — `atl/utils/hash.hpp:269` | Function | 6 | 4,224 | euler-tour-tree-basic |
| `atl::traits::is_integral` — `atl/types/traits/integral.hpp:50` | Record | 32 | 4,032 | aho-corasick-quick |
| `atl::coordinate_compression::ranks` — `atl/ds/coordinate_compression.hpp:252` | Method | 2 | 3,974 | coordinate-compression-basic |
| `atl::detail::multiply_high_wide` — `atl/types/detail/wide_arithmetic.hpp:228` | Function | 5 | 3,945 | matrix-quick |
| `atl::flat_hash_map::insert` — `atl/ds/flat_hash_map.hpp:192` | Method | 5 | 3,740 | euler-tour-tree-basic |
| `atl::traits::is_one_of` — `atl/types/traits/is_one_of.hpp:10` | Record | 32 | 3,712 | aho-corasick-quick |
| `atl::traits::is_one_of_v` — `atl/types/traits/is_one_of.hpp:14` | Var | 33 | 3,366 | aho-corasick-quick |
| `atl::traits::is_unsigned_integral_v` — `atl/types/traits/integral.hpp:38` | Var | 35 | 3,255 | aho-corasick-quick |

#### AST

| Declaration / original location | Kind | N | nlm B | Example witness |
| --- | --- | ---: | ---: | --- |
| `atl::graph::scc` — `atl/graph/scc.hpp:29` | Function | 1 | 2,018 | bipartite-matching-dag |
| `atl::graph::vertex_partition::grouped` — `atl/graph/partition.hpp:76` | Method | 6 | 1,806 | bipartite-matching-dag |
| `atl::treap_sequence::treap_sequence` — `atl/ds/treap_sequence.hpp:491` | Method | 1 | 886 | treap-sequence-quick-start |
| `atl::graph::visit` — `atl/graph/traversal.hpp:23` | Record | 10 | 820 | bipartite-matching-dag |
| `atl::graph::vertex_partition::vertex_partition` — `atl/graph/partition.hpp:53` | Method | 2 | 818 | functional-graphs-quick |
| `atl::treap_sequence::treap_sequence` — `atl/ds/treap_sequence.hpp:477` | Method | 1 | 777 | treap-sequence-quick-start |
| `atl::treap_sequence::treap_sequence` — `atl/ds/treap_sequence.hpp:452` | Method | 1 | 446 | treap-sequence-quick-start |
| `atl::dense_transitions::dense_transitions` — `atl/strings/dense_transitions.hpp:103` | Method | 1 | 418 | aho-corasick-quick |
| `atl::flat_hash_map::operator[]` — `atl/ds/flat_hash_map.hpp:92` | Method | 2 | 382 | flat-hash-basic |
| `atl::detail::carry_result` — `atl/types/detail/wide_arithmetic.hpp:29` | Record | 5 | 375 | matrix-quick |
| `atl::detail::word_pair` — `atl/types/detail/wide_arithmetic.hpp:23` | Record | 5 | 345 | matrix-quick |
| `atl::treap_sequence::treap_sequence` — `atl/ds/treap_sequence.hpp:445` | Method | 1 | 342 | treap-sequence-quick-start |
| `atl::beats::merge::operator()` — `atl/ds/segment_tree_beats.hpp:418` | Method | 1 | 332 | segment-tree-beats-quick-start |
| `atl::beats::merge::operator()` — `atl/ds/segment_tree_beats.hpp:412` | Method | 1 | 303 | segment-tree-beats-quick-start |
| `atl::graph::vertex_partition` — `atl/graph/partition.hpp:45` | Record | 2 | 292 | functional-graphs-quick |

#### dependent name

| Declaration / original location | Kind | N | nlm B | Example witness |
| --- | --- | ---: | ---: | --- |
| `atl::detail::hash_table::end` — `atl/ds/detail/hash_table.hpp:226` | Method | 1 | 84 | time-divide-and-conquer-connectivity |

#### A12 library-spelled member

| Declaration / original location | Kind | N | nlm B | Example witness |
| --- | --- | ---: | ---: | --- |
| `atl::graph::lca::path` — `atl/graph/lca.hpp:486` | Method | 3 | 3,357 | lca-quick |
| `atl::detail::treap_base::split_at_node` — `atl/ds/detail/treap_base.hpp:257` | Method | 3 | 2,421 | persistent-treap-sequence-basic |
| `atl::flat_hash_map::emplace` — `atl/ds/flat_hash_map.hpp:241` | Method | 5 | 1,750 | euler-tour-tree-basic |
| `atl::detail::hashed_child_pool::for_each` — `atl/ds/detail/hashed_child_pool.hpp:170` | Method | 4 | 1,732 | palindromic-tree-quick |
| `atl::flat_hash_map::try_emplace` — `atl/ds/flat_hash_map.hpp:226` | Method | 5 | 1,525 | euler-tour-tree-basic |
| `atl::flat_hash_map::try_emplace` — `atl/ds/flat_hash_map.hpp:214` | Method | 5 | 1,495 | euler-tour-tree-basic |
| `atl::detail::radix_index::digit` — `atl/ds/detail/radix.hpp:54` | Method | 4 | 1,444 | dsu-history |
| `atl::splay_engine::for_each` — `atl/ds/splay_engine.hpp:670` | Method | 2 | 1,420 | subtree-link-cut-tree-basic |
| `atl::splay_engine::rotate` — `atl/ds/splay_engine.hpp:542` | Method | 5 | 1,350 | splay-engine-quick-start |
| `atl::montgomery::reduce` — `atl/nt/montgomery.hpp:70` | Method | 4 | 1,244 | matrix-quick |
| `atl::static_modulus::power` — `atl/nt/static_modulus.hpp:116` | Method | 5 | 1,170 | matrix-quick |
| `atl::detail::splay_composites::erase` — `atl/ds/splay_engine.hpp:285` | Method | 3 | 1,125 | splay-engine-quick-start |
| `atl::flat_hash_map::contains` — `atl/ds/flat_hash_map.hpp:276` | Method | 5 | 1,120 | euler-tour-tree-basic |
| `atl::barrett64::mul` — `atl/nt/modular.hpp:196` | Method | 4 | 976 | power-monoid |
| `atl::flat_hash_map::insert_or_assign` — `atl/ds/flat_hash_map.hpp:258` | Method | 5 | 970 | euler-tour-tree-basic |

#### D3e rule 1 kinds

| Declaration / original location | Kind | N | nlm B | Example witness |
| --- | --- | ---: | ---: | --- |
| `atl::hash::operator()` — `atl/utils/hash.hpp:301` | Method | 6 | 4,698 | euler-tour-tree-basic |
| `atl::hash::operator()` — `atl/utils/hash.hpp:451` | Method | 6 | 2,526 | euler-tour-tree-basic |
| `atl::hash::operator()` — `atl/utils/hash.hpp:472` | Method | 6 | 2,454 | euler-tour-tree-basic |
| `atl::hash::operator()` — `atl/utils/hash.hpp:404` | Method | 4 | 2,060 | euler-tour-tree-basic |
| `atl::hash::operator()` — `atl/utils/hash.hpp:429` | Method | 6 | 1,662 | euler-tour-tree-basic |
| `atl::graph::lca::operator()` — `atl/graph/lca.hpp:384` | Method | 2 | 1,420 | functional-graphs-quick |
| `atl::detail::top_tree_impl::top_tree_impl` — `atl/ds/top_tree.hpp:1615` | Method | 1 | 1,231 | top-tree-basic |
| `atl::detail::top_tree_impl::top_tree_impl` — `atl/ds/top_tree.hpp:1599` | Method | 1 | 1,152 | top-tree-basic |
| `atl::geometry::point::operator-=` — `atl/geometry/point.hpp:133` | Method | 11 | 1,133 | calipers-quick |
| `atl::graph::vertex_groups::operator[]` — `atl/graph/partition.hpp:30` | Method | 5 | 1,060 | bipartite-matching-dag |
| `atl::geometry::point::operator*=` — `atl/geometry/point.hpp:140` | Method | 11 | 1,045 | calipers-quick |
| `atl::hash::operator()` — `atl/utils/hash.hpp:367` | Method | 6 | 1,032 | euler-tour-tree-basic |
| `atl::static_vector::static_vector` — `atl/ds/static_vector.hpp:85` | Method | 3 | 1,029 | circle-quick |
| `atl::convex_piecewise_linear::operator()` — `atl/dp/convex_piecewise_linear.hpp:527` | Method | 2 | 976 | convex-piecewise-linear-moving |
| `atl::hash::operator()` — `atl/utils/hash.hpp:235` | Method | 6 | 930 | euler-tour-tree-basic |

#### never-removed operator

| Declaration / original location | Kind | N | nlm B | Example witness |
| --- | --- | ---: | ---: | --- |
| `atl::geometry::dual_support_view` — `atl/geometry/calipers.hpp:253` | Record | 1 | 1,131 | delaunay-quick |
| `atl::static_vector` — `atl/ds/static_vector.hpp:33` | Record | 1 | 1,093 | delaunay-quick |
| `atl::geometry::antipodal_view` — `atl/geometry/calipers.hpp:86` | Record | 1 | 895 | delaunay-quick |
| `atl::geometry::lattice_count` — `atl/geometry/polygon.hpp:155` | Record | 4 | 784 | calipers-quick |
| `atl::geometry::convexity` — `atl/geometry/polygon.hpp:35` | Record | 3 | 540 | circle-quick |
| `atl::geometry::strip` — `atl/geometry/calipers.hpp:743` | Record | 2 | 420 | calipers-quick |
| `atl::geometry::segment_intersection` — `atl/geometry/line.hpp:249` | Record | 1 | 291 | delaunay-quick |
| `atl::geometry::support_event` — `atl/geometry/calipers.hpp:213` | Record | 1 | 286 | delaunay-quick |
| `atl::geometry::antipodal_event` — `atl/geometry/calipers.hpp:51` | Record | 1 | 237 | delaunay-quick |
| `atl::geometry::segment` — `atl/geometry/segment.hpp:16` | Record | 1 | 199 | delaunay-quick |
| `atl::geometry::<deduction guide>` — `atl/geometry/calipers.hpp:424` | Guide | 1 | 196 | delaunay-quick |
| `atl::geometry::line` — `atl/geometry/line.hpp:25` | Record | 1 | 190 | delaunay-quick |
| `atl::geometry::<deduction guide>` — `atl/geometry/calipers.hpp:420` | Guide | 1 | 179 | delaunay-quick |
| `atl::geometry::<deduction guide>` — `atl/geometry/calipers.hpp:190` | Guide | 1 | 141 | delaunay-quick |

#### roots/effects

| Declaration / original location | Kind | N | nlm B | Example witness |
| --- | --- | ---: | ---: | --- |
| `atl::commutative_fold` — `atl/types/algebraic.hpp:168` | Var | 27 | 1,485 | aggregate-queue-deque-sliding-window |
| `atl::commutative_tags` — `atl/types/algebraic.hpp:143` | Var | 25 | 1,375 | aggregate-queue-deque-sliding-window |
| `atl::detail::counting_sort_buffers` — `atl/utils/counting_sort.hpp:97` | Function | 9 | 1,080 | aho-corasick-quick |
| `atl::modular_integer` — `atl/types/concepts.hpp:46` | Concept | 1 | 704 | modular-arithmetic-residues |
| `atl::graph::skip` — `atl/graph/traversal.hpp:792` | Var | 12 | 672 | bipartite-matching-dag |
| `static_assert` — `atl/types/detail/wide_arithmetic.hpp:20` | StaticAssert | 7 | 623 | crt-quick |
| `atl::extended_gcd` — `atl/nt/gcd.hpp:40` | Function | 1 | 458 | modular-arithmetic-residues |
| `atl::power` — `atl/nt/power.hpp:26` | Function | 1 | 397 | modular-arithmetic-residues |
| `atl::extended_gcd` — `atl/nt/gcd.hpp:80` | Function | 1 | 390 | modular-arithmetic-residues |
| `atl::montgomery::mul_lazy_fixed` — `atl/nt/montgomery.hpp:176` | Method | 1 | 377 | modular-arithmetic-residues |
| `atl::montgomery::add` — `atl/nt/montgomery.hpp:94` | Method | 1 | 322 | modular-arithmetic-residues |
| `atl::static_modulus::add` — `atl/nt/static_modulus.hpp:52` | Method | 1 | 322 | modular-arithmetic-residues |
| `atl::montgomery::reduce_lazy` — `atl/nt/montgomery.hpp:143` | Method | 1 | 315 | modular-arithmetic-residues |
| `atl::pow_mod` — `atl/nt/modular.hpp:296` | Function | 1 | 304 | modular-arithmetic-residues |
| `atl::mul_mod_unchecked` — `atl/nt/modular.hpp:90` | Function | 1 | 300 | modular-arithmetic-residues |

#### pinned

| Declaration / original location | Kind | N | nlm B | Example witness |
| --- | --- | ---: | ---: | --- |
| `atl::detail::uses_portable_wide_arithmetic` — `atl/types/detail/wide_arithmetic.hpp:19` | Var | 7 | 609 | crt-quick |

### (b) Preprocessor text

| Text | Distill occurrences / B | Caide occurrences / B | Net B |
| --- | ---: | ---: | ---: |
| Generated `DISTILL_ONCE_…` defines | 1,187 / 45,106 | 0 / 0 | 45,106 |
| System includes | 4,525 / 88,423 | 1,863 / 35,638 | 52,785 |
| Other directives | 211 / 15,417 | 113 / 12,786 | 2,631 |
| Conditional directives | 0 / 0 | 76 / 2,742 | −2,742 |
| Inactive non-directive branch text | 0 / 0 | 471 B | −471 |
| Generated line markers | 0 / 0 | 0 / 0 | 0 |
| **Total** | | | **97,309** |

All 100 distill outputs are **pruned**, not flat/raw fallbacks. By the pruning gate, no unresolved user conditional survives; the reports and output directives agree. Therefore there is **no positive unknown-branch tax** in these 100, unlike earlier all-126 geometry analyses. Caide preserves some `_MSC_VER`, GNU-prefetch and wide-arithmetic alternatives. The 471 B negative branch row uses the finite conditions in those artifacts and the CSES resolution facts; directive lines, including inactive `#define`/`#include` lines, remain in their directive rows. It does not promote a worker observation into a new judge fact.

The include delta is **2,669 repeated include lines / 52,925 B** in distill, offset by **7 caide-only `<intrin.h>` lines / 140 B** in inactive MSVC branches. Caide hoists/deduplicates; distill preserves include order. Extra generated defines occur once each in surviving user text, with **no subsequent user-token use**. Other-directive differences are `ATL_HAS_INT128` (net +2,175 B), `ATL_DETAIL_WIDE_CARRY_DIVIDE_FAST` (+264), and `ATL_DETAIL_WIDE_RUNTIME_BACKEND` definitions/undefinitions (+192). `atl_assert` macro bodies cancel. Raw tokens in such macros still matter to name retention.

Largest repeated includes (not additional ledger rows):

| Include | Repeated occurrences | nlm B |
| --- | ---: | ---: |
| `#include <type_traits>` | 535 | 12,305 |
| `#include <concepts>` | 428 | 8,560 |
| `#include <cstddef>` | 320 | 6,080 |
| `#include <utility>` | 287 | 5,453 |
| `#include <vector>` | 206 | 3,708 |
| `#include <ranges>` | 196 | 3,528 |
| `#include <limits>` | 189 | 3,402 |
| `#include <functional>` | 133 | 2,926 |
| `#include <algorithm>` | 95 | 1,995 |
| `#include <cstdint>` | 45 | 855 |
| `#include <tuple>` | 47 | 799 |
| `#include <array>` | 36 | 612 |
| `#include <span>` | 32 | 512 |
| `#include <cmath>` | 27 | 459 |
| `#include <iterator>` | 20 | 400 |

### (c) Formatting and structural text

| Text after declaration/directive ownership | Net B |
| --- | ---: |
| Whitespace in the residual of shared declarations | 22,671 |
| Whitespace outside shared declarations | 64,085 |
| Namespace shells/spelling/braces | 4,075 |
| Caide-only dangling `[[nodiscard]]` attribute tokens | −182 |
| **Total** | **90,649** |

The whitespace difference is **86,756 B** under this ownership convention. Whitespace *inside* (a)/(d) declaration spans is already charged there. Caide leaves 14 extra `[[nodiscard]]` spellings attached to later declarations after deleting intervening declarations; those are the entire residual nonwhitespace difference inside paired declarations after (d). Their −182 B is counted here. No generated line-marker contribution is hidden in formatting.

### (d) Deletions inside shared declarations

| Structural subdeclaration removed by caide | N | Owned nlm B |
| --- | ---: | ---: |
| Hidden friend functions/operators | 85 | 11,641 |
| Aliases / nested typedefs | 182 | 5,676 |
| Nested records | 22 | 5,411 |
| Static data / constants | 41 | 3,100 |
| Assertions, including one in `main` | 13 | 1,132 |
| Non-function friend declaration | 1 | 38 |
| **Total** | **344** | **26,998** |

These are carved out only when both tools retain the enclosing declaration. Separately extracted D3e methods belong to (a), never to this table. Examples: `mod_int` hidden stream/arithmetic operators; `static_vector` comparison friends and public aliases; `hash_table`'s nested iterator structure; Montgomery constants/assertions. No arbitrary statement-level partial-body trimming was found after accounting for these complete subdeclarations, conditional branches and dangling attributes. The `bit-utilities-combinations` local assertion is inside the retained `main`, not a free declaration.

The negative **19,636 B / 83** offset includes caide-kept declarations distill removes: notably a `treap_map` occurrence (4,817 B), `scannable_graph` concepts (2,838), `make_unsigned` records (2,238), `make_signed` records (2,199), and `copy_cv` records (1,230). The DP cases' caide repairs retain broad name closures; that helps explain the three reversed gaps.

## What another compatible judge needs

“Compatible” means a GCC/libstdc++ configuration satisfying the selected profile facts and A1–A7/A9 where used, A11, A12 and D3f, **not** an arbitrary platform with contradictory preprocessing facts. A fixed `#if` decision is not reopened to manufacture a counterexample. Conversely, equal user preprocessing does not fix `sizeof`, library typedefs, overload viability, constraints, traits, layout or which conforming library path instantiates a user API. A missing worker AST edge cannot certify absence of judge-side lookup.

The judgments below are **(i) retain for the current soundness argument**, **(ii) conservative, with a plausible narrower rule**, or **(iii) not decidable from these reports**. A reason group is not a set of bytes all proven necessary on every judge. No minimum cross-judge source-size number can be recovered from first-cause reports.

| Group | Judgment | Concrete mechanism, narrower rule and evidence needed |
| --- | --- | --- |
| Name rule, 342,626 B | Mixed (i)/(ii) | Retained uninstantiated templates must still parse, including their helper names. `mod_int::operator>>` spells `read_stream_magnitude`; removing just the helper breaks lookup even if the stream body is uninstantiated. By contrast `coordinate_compression::ranks` collides with `compressed::ranks` fields/locals: 2 occurrences / 3,974 B. Prove a target-stable foreign binding before suppressing that name witness; `auto`, selected aliases/bases and dependent receivers need a stronger proof than the worker binding. A separate raw audit finds 6 occurrences / 2,018 B with only other records' method-declarator names as external witnesses. The current artifacts again show `ranks` only as the other record's field and as local-vector names outside that method. Both pools still need AST/pin/effect vetoes checked. |
| Association, 0 B | No measured cost | D3f is already enabled. Do not present reimplementing unspelled namespace functions as a new saving on this corpus. Hidden friends and operators remain excluded for a reason. |
| AST, 13,611 B | (i) locally necessary closure; (iii) whether owner can disappear | Positive references retain these declarations. `scc` in bipartite-matching-dag is reached through a retained helper; `vertex_partition::grouped` supplies constructor paths. Some callers may disappear with a broader deletion group, but deleting the callee alone is not justified. Need the complete graph and surviving-reference closure, not a list of worker runtime calls. |
| Dependent name, 84 B | (i), pending a receiver proof | `hash_table::end` in time-divide-and-conquer-connectivity has a retained dependent-name witness. Another substitution/library path can resolve that spelling differently. Keep unresolved sites and all possible owners; do not restrict them to observed instantiations. |
| A6/ADL, 0 B | No measured direct cost; (i) protect the mechanism | The absence of a row is not permission to weaken A6. Free/hidden operators are accounted under structural owners and (d); language/library ADL and failed probes remain relevant. |
| A12 library-spelled members, 105,222 B | (i) under today's proof; substantial (ii) headroom | `path`, `rotate`, `digit`, `split_at_node` can match unrelated library tokens, while `size`, `reserve`, `empty`, `begin` can be real container/range hooks. `hash_table`/persistent structures expose range and allocator-related surfaces; a library can probe or instantiate APIs absent from the worker's executed path. A12 bounds **spelling**, not which record a spelling can target. Narrow by an audited hook/receiver certificate including inactive paths, macro expansion and all scanned builds; do not simply replace the union by reached worker functions. |
| D3e rule 1, 57,531 B | (i) for general removal; (iii) individual unused overloads | Constructors affect constructibility, conversion sets, CTAD, copy/move availability and aggregate status; operators affect failed constraints as well as successful calls. ATL `static_vector`, `top_tree_impl` constructors and `hash::operator()` are real surfaces. A narrower rule needs an all-substitution/target candidate-set proof or an explicit new assumption excluding those effects. Worker used/referenced flags cannot supply it. |
| Never-removed operators, 6,582 B | (i) general protection; (iii) individual owners | Hidden comparison/iteration friends root some otherwise unused records. Operator candidates can alter ambiguity or rewrite a comparison without a resolved reference. [The operator reachability review](2026-11-18-operator-reachability.md) supplies a counterexample surviving A7; do not infer removability from an unused parameter record. |
| Roots/effects, 17,895 B | (i) for general roots/effects; (ii) for narrow empty tags | Non-inert static storage can initialize/destruct observably. A11 excludes specified constexpr/destruction and unreached-block-static effects, not arbitrary dynamic storage. `dynamic_mod_int::reducer_` makes the 12,065 B possible-trigger closure meaningful; a different type/engine selection can trigger it. The `counting_sort_buffers` root also merits a smaller A11 audit: absence of calls must be proved before treating its block-scope static storage as never reached. Separately, `commutative_tags`/`commutative_fold` have empty user tag types with an in-class explicitly defaulted constructor: a positive syntactic extension to D3d-3 can address that concrete case without assuming all constexpr/constinit objects inert. |
| Pinned, 609 B | (i) retain the pin unless its syntactic obligation is proved absent | `uses_portable_wide_arithmetic` reports **`pinned: macro invocation`**, involving the backend macro in its declaration. This is not unresolved-conditional overhead. Retain expansion ownership and dependencies; a narrower macro-interior rule must prove the complete syntactic unit can be erased without crossing a shared invocation or another root/pin. |
| Other, 0 B | No unidentified (a) group | The small identity remainder is disclosed in the method and cancels, rather than being declared a saving. |
| Generated guard defines, 45,106 B | (ii) | User-text use audit is negative for all 1,187. Resolution has consumed the once tests. Removing definitions additionally requires proving no remaining header/macro can observe these invented names. A3 excludes predefined collisions and A2 protects definitions, but neither literally forbids a header from **testing** a user macro. Add a narrow generated-macro non-observation assumption or an adequate raw/expanded header audit; do not call these deletions already authorized by A3 alone. |
| Repeated includes, +52,925 B gross | (ii) | Preserve the first include and its position. A1/A2/A9 do not say repeated headers are idempotent: e.g. `<cassert>` can react to `NDEBUG`; C headers can have macro-controlled interfaces. An unchanged-relevant-macro-state/idempotence certificate could remove duplicates. Do not adopt caide's global hoisting as a proof; headers between a macro definition and its use can change lookup or macro state. |
| Other directives, +2,631 B net | (ii)/(iii) | Feature/backend defines are often dead after branch resolution, but later includes or macro expansions can observe them. Need a use/state audit including headers, analogous to generated guard removal, while preserving the `atl_assert` binding and arguments. |
| Unresolved user branches / line markers, 0 B | No removable positive pool here | Unknown user conditionals would disable pruning; line omission already passed its position-observation gate. Caide's −3,213 B of conditional syntax/inactive text is a countervailing cost, not distill overhead. |

For (d), data members/nested types can determine layout, traits and overload validity on another target; aliases such as iterator/category/value types can be named by another library. Hidden friends obey the operator/ADL argument above. Assertions constrain compilation of retained types, even if a well-formed-input contract can justify some assertion removal after separate dependency analysis. A blanket “delete unreferenced members inside a live class” would skip these obligations.

### Caide soundness flags and limits

**Flagged unsafe as a general cross-judge policy:** deleting unselected constructor/operator candidates, dropping otherwise-unreferenced nested aliases/data from live records, or deciding dependent alternatives solely from the worker AST. Concrete ATL exposures in these artifacts are the `static_vector` aliases/comparison friends, `point` arithmetic friends, `hash` call operators and type-width trait specializations. Another library can issue a trait/protocol probe, or another allowed type/layout selection can take an alternative template path. The [member-call analysis](2026-11-21-member-call-analysis.md) and [trim analysis](2026-11-22-trim-analysis.md) document target-selected alias/constructor counterexamples under the same assumptions. These are reasons a portable optimizer must retain a proof barrier, not proofs that every listed ATL declaration is needed.

**No concrete alternate-judge failure of one of these 100 complete submissions was established.** The supplied reports neither enumerate all compatible judges nor retain the complete semantic graph. Worker verification/repair success cannot close that gap, but it would also be wrong to label every caide deletion an observed cross-judge bug. In particular, never assume an unused floating-hash specialization becomes used merely because `long double` changes; the surviving program must actually reach the protocol. The clear result is an unsound general deletion criterion and concrete risk surfaces, not a fabricated failing ATL run.

## Ranked new-rule candidates

Ranked by **measured direct nlm pool**, not claimed achieved savings. The first two are ceilings requiring much more proof; they cannot be added as safe gains. Immediate reasons overlap alternative protections not printed by the report. All rules must preserve root/pin checks, reverse potential-effect edges, A7/A11, preprocessing gates and actual output validation; refine the static-name invariant along with any name rule.

| Rank | Proposed rule / assumption wording | Measured nlm pool | Evidence and risk |
| --- | --- | ---: | --- |
| 1 | **Target-stable member-name bindings.** “A foreign declarator/local/field token is not a witness for member M if its binding and receiver exclusion are invariant on every allowed judge; unresolved or target-selected lookup retains M.” | **156,977 B / 699 declarations ceiling**; **5,992 B / 8 declarations** concrete audit kernel | The kernel is the 3,974 B `ranks` collision previously semantically audited, plus 2,018 B with only foreign method declarators as raw witnesses. This is an extension of D3f's spelling argument to proved foreign bindings, not permission to prune 156,977 B. Macro-pasted names, aliases, defaults, constraints, using/friend references, AST edges and effects are vetoes until audited. No new trust assumption is necessary for a real syntactic/all-target proof; a worker-only binding shortcut would require an explicit stronger assumption and is not recommended. |
| 2 | **A12 with a certified member-call surface.** Proposed, not adopted: “Judge headers name user members only at audited language/library hook sites and receiver/type flows covered by the certificate for this TU, including dormant implementation paths; an unrelated declaration token is not a hook.” | **105,222 B / 607 declarations ceiling**; **0 B certified removable here** | Largest inventory examples include `lca::path` and treap/splay/hash APIs. Need header call-site/receiver provenance across scanned builds, expanded macros, inactive branches and future-compatible builds, with fail-closed handling. This narrows A12 and risks missing a dormant judge path; reached-worker-facility matching alone is insufficient. |
| 3 | **Repeated-header idempotence.** Proposed: “A repeated approved system header has no additional declaration/macro/pragma effect on any allowed judge when all of its relevant macro/include state is unchanged.” | **52,925 B / 2,669 include lines** | Keep first occurrences in place; audit transitive relevant state or explicitly assume idempotence for a whitelist. `<type_traits>`, `<concepts>`, `<cstddef>` dominate. Exclude configuration-sensitive C headers and macro changes until certified. Net gap contribution is 52,785 B because caide also carries inactive `<intrin.h>` includes. |
| 4 | **Erase consumed generated guards.** Proposed: “Judge headers and macros never observe collision-checked generated once-guard names; after resolution no surviving user test depends on their definitions.” | **45,106 B / 1,187 defines** | Every surviving guard has only its defining user token; a header/inventory non-observation certificate or explicit assumption remains necessary. Preserve source positions when observed, collision checking and inliner recursion/once proofs. Erase only after resolution; never change raw/flat checking semantics prematurely. |
| 5 | **Empty tags with a defaulted constructor are inert.** Extend D3d-3's direct, complete non-template class case to permit exactly an in-class first-declaration `explicit T() = default`, with no other user-declared special member, bases, fields, virtuals, unions, attributes or dependent context; preserve the other constexpr/static-storage exclusions. | **6,560 B / 152 declarations** in a raw-name deletion closure | `commutative_tags`/`commutative_fold`: 52 objects / 2,860 B; 50 type shells / 1,700 B; 50 constructors / 2,000 B. Full raw-name closure finds no external witness after removing the group. Initialization is positively trivial in this exact spelling; A7/A11 already bound the stated effects. Requires production graph replay and pinned-worker regressions, not a broad `isTriviallyDestructible`/constexpr/constinit inference. |

The **57,531 B** rule-1 pool and **11,641 B** of interior hidden friends are not ranked as easy new rules: “worker never calls this overload” is already known to be unsound. The 81,741 B of name-retained free functions includes real helper lookups inside protected owners; shrinking an owner's protection can have a cascade, but deleting those helpers first is invalid. No cascade estimate is fabricated from first-cause reports.

What is demonstrably optional now is the existing **271,873 B** compaction saving. Repeated includes plus consumed guards offer a further **98,031 B** of identifiable preprocessor text **if** their new certificates/assumptions are accepted. Those directive lines are unchanged by whitespace compaction, so the two gains are disjoint: compact plus those two hypothetical rules would leave a **369,576 B** gap against this caide baseline. That remainder is not a count of semantically necessary bytes. Its minimization needs the graph/binding/hook work above.

## Verification and reproduction

No compiler, SDK download or C++ build was required. Only the requested review and analysis-only Python helpers were added. Read-only inputs were not modified; all intermediate JSON/logs/dependencies are worktree-local under ignored `measurements/caide-gap/`.

[Analysis helpers](caide-gap-analysis/analyze.py), [ledger](caide-gap-analysis/ledger.py), [finite branch accounting](caide-gap-analysis/branches.py), [candidate audit](caide-gap-analysis/candidates.py), [checks](caide-gap-analysis/check.py) reproduce the tables. From the worktree root, with the two pinned Python wheels installed locally, run:

```sh
mkdir -p measurements/caide-gap/tmp measurements/caide-gap/python
TMPDIR="$PWD/measurements/caide-gap/tmp" python3 -m pip install --no-cache-dir \
  --target measurements/caide-gap/python tree-sitter==0.27.0 tree-sitter-cpp==0.23.4
python3 docs/reviews/caide-gap-analysis/analyze.py
python3 docs/reviews/caide-gap-analysis/ledger.py
python3 docs/reviews/caide-gap-analysis/candidates.py
python3 docs/reviews/caide-gap-analysis/check.py
```

The first two scripts accept optional input-root and output-directory arguments; the same arguments may be passed to the candidate/check scripts. Default input is the task's supplied path; default output is ignored `measurements/caide-gap/`. No production extractor patch is involved.

Checks passed: **100 independent per-example ledger reconciliations**, all **400** selected artifact sizes against TSV, **200** error-free offset-preserving syntax views, **100 exact independent compact-output reproductions**, inclusion of **11** repair examples, **1,187** guard-token use audits, and exact cancellation of non-shell unmatched declaration text. Shared-declaration residuals agree after accounting for precisely the dangling `[[nodiscard]]` tokens. These are analysis consistency tests; no new compile/run or alternate-judge validation is claimed. Supplied CI compile/sample results remain the runtime evidence.

Input `summary.tsv` SHA-256: `f53c1d92fc98397d051bf3f60da81c69c826138b12c4901452c53ab7370adb05`.
