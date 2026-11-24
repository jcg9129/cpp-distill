<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Free functions and hidden friends: candidate-rule analysis

2026-11-24. Analysis only. Production base `a653b6c`; checked-out `3fac677` differs only in documentation. Read-only ATL `5b14f2d` (126 examples). No production policy change, commit or push.

**Progress: 126/126 generated, 126/126 analyzed; 126/126 standalone sample checks pass.** Updated 05:36:04 UTC. Final corpus totals.

## Measurement and method

Reuses [the earlier free-function analysis](2026-11-22-trim-analysis.md#free-functions-and-hidden-friend-candidates) and its diagnostic patch. CSES `cses-gcc13-cpp23`, C++23, `-O2 --strip-comments --no-line-markers`, default A11/A12, following `ci/atl-sweep/sweep.sh`. Compiler: relocated x86_64 GCC 13.4, ARM64-hosted; standard-library headers are this compiler's headers, not a CSES GCC 13 capture. All inputs are read-only. Two examples maximum run concurrently; generation/compile/run caps 240/120/30 seconds, generation and compilation address-space cap 2,500,000 KiB. SDK LLVM 23.1.5 ARM64, release-SHA256SUMS verified: `5ca712f18c710592a8fa89758246a6b6cbdbd16c1fbd5f12c7f5bcdf35fe06de`; worktree-local download/extraction. Build `-O1 -DNDEBUG`, Clang 22.1.8, LLD, `-j2`, 5,700,000 KiB virtual-memory cap, hard configure/build caps 120/900 seconds. All six focused CTest suites pass (`unit`, `embedded_members`, `prune_extract_cases`, `prune_extract_models`, `member_pruning`, `member_review`). The SDK archive and extraction were deleted after the diagnostic/baseline builds and tests.

The reused GCC 15.2 setup initially reproduced geometry flat fallbacks: its C++23 `-O2` checking configuration has no matching scanned standard-macro facts, so satisfaction demotes `FLT_EVAL_METHOD`. Those trial outputs are isolated in ignored `measurements/trim-gcc15/` and excluded here. GCC 13.4 matches the captured GCC-13 standard-macro input scope (`gcc-13@8255e82cc53970e5`); the complete measurement sweep was restarted with it. This is a compiler-configuration correction, with no gate waiver or policy modification.

Counts are written declaration **occurrences across examples**, not unique header definitions. The initial pool excludes `main`, standalone root/pinned functions, any actual kept resolved reference (including unevaluated references and references from reached system entities), and any compiler used/referenced flag, conservatively including residual flags from discarded code. Template instances merge flags into their written declaration. Function-template headers, attributes and terminators belong to declarations; lambdas/function-local declarations do not become free-function candidates. Hidden friends are carved out of kept structural records. Function-local declarations belong to their enclosing written free declaration. Current main separately extracts three local comparator operators inside `locations_of` (`geometry/point_location.hpp:570`); the adapted group includes them, adding 309 nlm / 282 compact bytes, and removes the complete function. They are local member bodies, not non-member operator candidates under (a).

The renderer copy map maps immutable input bytes to the actual nlm artifact; bytes absent from the artifact count zero. Compact counts map those same owned bytes through **production** `compactWhitespace`; the diagnostic checks exact output equality and the complete token/directive invariant. This is owned-byte attribution, excluding surrounding indentation, line breaks or renderer separators outside the declaration. Total nlm output: **4,535,428 B**; compact: **4,051,059 B**. Whole-artifact deletion gains can differ slightly because separators/trivia survive or disappear.

Starting with the whole pool, a simultaneous greatest-deletion fixpoint restores a declaration whenever its identifier name occurs outside the deletion set in retained raw tokens **or** the user preprocessed token stream. Both include uninstantiated bodies, signatures/defaults, constraints, macro definitions and arguments; strings/comments are excluded. A strict run additionally keeps operators, conversions, literal operators, guides, `main`, C-linkage and ADL-listed names. A relaxed run omits only these (a) exclusions. Production's actual reverse potential-effect traversal is recorded separately; any candidate in an effect-reachable aggregate or root/pin is excluded. Thus G1 satisfies the sketch within the existing A6/A7/A11 and preprocessing contracts; this is analysis, not a cross-library formal proof.

## Recomputed pool

| Partition | Declarations | Owned nlm B | Owned compact B |
| --- | ---: | ---: | ---: |
| G1 | 178 | 148,859 | 137,552 |
| G2 | 242 | 38,017 | 36,454 |
| G3 | 451 | 187,776 | 176,607 |
| G4 | 19 | 45,451 | 41,437 |
| blocked c/d | 23 | 5,130 | 4,781 |
| **Initial pool** | **913** | **425,233** | **396,831** |

Comparable lexical-only closure (before c/d): **453 declarations / 235,171 nlm B / 218,118 compact B**. Of these, **14 / 2,844 nlm B / 2,675 compact B** are rejected by c/d. G1 is **63.30%** of this recomputed lexical-only nlm pool. The other c/d-rejected declarations also have external name witnesses.

G1 is the strict candidate gain. G2 consists of (a)-protected declarations in the relaxed lexical closure. G4 consists of ordinary helpers removed in the relaxed closure but kept by the strict one; their only remaining name users are within G2/G4. G3 is rejected even by the relaxed closure because identifiers remain elsewhere. These rows are disjoint; G1+G2+G4 is the lexical pool after the additional (c)/(d) barriers. The `blocked c/d` row is necessary: the four requested labels do not cover a declaration rejected by potential effects but not by (a) or (b). These bytes are not silently charged to G2 or G3. The earlier 185-declaration / 102,265-byte pool was lexical-only, so a fully comparable count must also include effect-rejected declarations that pass lexical closure; the comparable count is given above. G2 and G4 describe a hypothetical deletion group with the (a) barrier waived; a G2 owner can name a G4 helper.

A hidden friend is **not by itself excluded by (a)**. The G2 hidden-friend rows below identify the actual reason (operator or ADL name).

| G2 exclusion | Declarations | nlm B | compact B |
| --- | ---: | ---: | ---: |
| operator | 0 | 0 | 0 |
| hidden friend operator | 242 | 38,017 | 36,454 |
| ADL-listed name | 0 | 0 | 0 |
| hidden friend ADL name | 0 | 0 | 0 |
| other | 0 | 0 | 0 |

G3 witness categories below are overlapping: one declaration can have both a same-name overload and a requires-expression witness.

| External witness category | Declarations with witness | nlm B | compact B |
| --- | ---: | ---: | ---: |
| same-name declaration/overload | 201 | 68,738 | 64,931 |
| kept body/signature (including uninstantiated) | 433 | 183,992 | 173,005 |
| macro | 7 | 2,349 | 2,238 |
| requires-clause | 64 | 19,997 | 19,020 |
| coincidental member/local/parameter | 96 | 26,912 | 25,551 |
| kept structural/declaration text | 42 | 13,562 | 13,034 |

## G1: top 15 written declarations

| Source declaration | Occurrences | nlm B | compact B | Current reason / external witness |
| --- | ---: | ---: | ---: | --- |
| `atl/graph/traversal.hpp:1007` `atl::graph::dfs_fold` | 12 | 20,556 | 19,068 | associated with atl::graph::visit ← atl::graph::visit (12) |
| `atl/nt/basic.hpp:137` `atl::factorization` | 12 | 12,396 | 10,944 | associated with atl::prime_factor ← atl::prime_factor (12) |
| `atl/utils/counting_sort.hpp:108` `atl::detail::counting_permutation` | 9 | 9,639 | 8,712 | associated with atl::detail::counting_sort_scratch ← atl::detail::counting_sort_scratch (9) |
| `atl/geometry/polygon.hpp:258` `atl::geometry::winding_of` | 6 | 7,032 | 6,816 | associated with atl::geometry::winding ← atl::geometry::winding (6) |
| `atl/geometry/polygon.hpp:218` `atl::geometry::polygon_centroid` | 7 | 6,818 | 6,447 | associated with atl::geometry::point ← atl::geometry::point (7) |
| `atl/geometry/polygon.hpp:66` `atl::geometry::convexity_of` | 4 | 6,364 | 5,864 | associated with atl::geometry::convexity ← atl::geometry::convexity (4) |
| `atl/geometry/convex_polygon_operations.hpp:576` `atl::geometry::union_of_convex_polygons` | 1 | 5,762 | 5,225 | associated with atl::geometry::point ← atl::geometry::point (1) |
| `atl/geometry/convex_polygon_operations.hpp:750` `atl::geometry::closest_points_between_convex_hulls` | 2 | 4,304 | 4,008 | associated with atl::geometry::point ← atl::geometry::point (2) |
| `atl/graph/traversal.hpp:1050` `atl::graph::dfs_fold` | 12 | 3,984 | 3,912 | family aggregation ← atl::graph::dfs_fold (12) |
| `atl/geometry/point.hpp:365` `atl::geometry::detail::float64_difference` | 10 | 3,860 | 3,610 | associated with atl::geometry::point ← atl::geometry::point (10) |
| `atl/geometry/circle.hpp:531` `atl::geometry::minimum_enclosing_circle` | 2 | 3,250 | 2,914 | associated with atl::geometry::circle ← atl::geometry::circle (2) |
| `atl/geometry/line.hpp:279` `atl::geometry::intersection` | 2 | 3,098 | 2,790 | family aggregation ← atl::geometry::intersection (2) |
| `atl/geometry/convex_hull.hpp:97` `atl::geometry::detail::prune_octagon` | 1 | 3,081 | 2,775 | name: prune_octagon ← atl::geometry::detail::hull_indices (1) |
| `atl/geometry/circle.hpp:394` `atl::geometry::common_tangents` | 2 | 2,950 | 2,698 | associated with atl::static_vector ← atl::static_vector (2) |
| `atl/geometry/line.hpp:172` `atl::geometry::line_from_coefficients` | 2 | 2,890 | 2,810 | associated with atl::geometry::line ← atl::geometry::line (2) |

## G2: top 15 written declarations

| Source declaration | Occurrences | nlm B | compact B | Current reason / external witness |
| --- | ---: | ---: | ---: | --- |
| `atl/nt/mod_int.hpp:165` `atl::operator>>` | 18 | 7,452 | 7,002 | never removed: operator (15); family aggregation ← atl::cplx (1); family aggregation ← atl::static_vector (1); family aggregation ← atl::bit_matrix (1) |
| `atl/nt/mod_int.hpp:148` `atl::operator<<` | 17 | 5,831 | 5,474 | never removed: operator (14); family aggregation ← atl::cplx (1); family aggregation ← atl::static_vector (1); family aggregation ← atl::bit_matrix (1) |
| `atl/ds/static_vector.hpp:230` `atl::operator<=>` | 19 | 4,180 | 3,990 | never removed: operator (19) |
| `atl/ds/static_vector.hpp:223` `atl::operator==` | 19 | 3,116 | 2,926 | never removed: operator (19) |
| `atl/geometry/point.hpp:156` `atl::geometry::operator*` | 18 | 1,530 | 1,530 | never removed: operator (18) |
| `atl/geometry/point.hpp:159` `atl::geometry::operator*` | 18 | 1,530 | 1,530 | never removed: operator (18) |
| `atl/geometry/point.hpp:153` `atl::geometry::operator-` | 16 | 1,472 | 1,472 | never removed: operator (16) |
| `atl/geometry/point.hpp:150` `atl::geometry::operator-` | 13 | 1,157 | 1,157 | never removed: operator (13) |
| `atl/nt/matrix.hpp:361` `atl::operator*` | 1 | 1,074 | 967 | family aggregation ← atl::static_vector (1) |
| `atl/geometry/point.hpp:147` `atl::geometry::operator+` | 12 | 1,068 | 1,068 | never removed: operator (12) |
| `atl/nt/matrix.hpp:395` `atl::operator*` | 2 | 1,036 | 964 | family aggregation ← atl::static_vector (1); family aggregation ← atl::bit_matrix (1) |
| `atl/geometry/segment.hpp:25` `atl::geometry::operator==` | 13 | 1,001 | 1,001 | family aggregation ← atl::geometry::point (13) |
| `atl/geometry/polygon.hpp:161` `atl::geometry::operator==` | 8 | 712 | 712 | family aggregation ← atl::geometry::point (8) |
| `atl/geometry/polygon.hpp:41` `atl::geometry::operator==` | 8 | 648 | 648 | family aggregation ← atl::geometry::point (8) |
| `atl/geometry/line.hpp:265` `atl::geometry::operator==` | 5 | 570 | 515 | family aggregation ← atl::geometry::point (5) |

## G3: top 15 written declarations

| Source declaration | Occurrences | nlm B | compact B | Current reason / external witness |
| --- | ---: | ---: | ---: | --- |
| `atl/nt/convolution.hpp:447` `atl::convolution_uint64` | 9 | 31,077 | 28,962 | kept body/signature (including uninstantiated); e.g. `atl/nt/detail/power_series.hpp:114`: `auto const product = convolution_uint64<Engine>(left_limb, right_limb);` |
| `atl/ds/detail/forward_element.hpp:47` `atl::detail::forward_element` | 30 | 10,170 | 9,810 | kept structural/declaration text; kept body/signature (including uninstantiated); e.g. `atl/ds/detail/forward_element.hpp:59`: `using forwarded_element_t = decltype(forward_element<R>(std::declval<std::ranges::range_reference_t<R>>()));` |
| `atl/utils/hash.hpp:270` `atl::detail::hash_float_value` | 12 | 8,448 | 7,848 | kept body/signature (including uninstantiated); e.g. `atl/utils/hash.hpp:309`: `return static_cast<std::size_t>(detail::hash_float_value(v, detail::hash_seed()));` |
| `atl/nt/gcd.hpp:81` `atl::extended_gcd` | 20 | 7,800 | 7,320 | same-name declaration/overload; kept body/signature (including uninstantiated); e.g. `atl/nt/gcd.hpp:41`: `constexpr gcd_result<U> extended_gcd(U const &a, U const &b) {` |
| `atl/nt/modular.hpp:63` `atl::mul_mod_binary` | 22 | 6,248 | 5,742 | kept body/signature (including uninstantiated); e.g. `atl/nt/static_modulus.hpp:108`: `return mul_mod_binary<value_type>(a, b, mod);` |
| `atl/geometry/predicates.hpp:320` `atl::geometry::detail::cross_determinant` | 10 | 5,360 | 4,960 | kept body/signature (including uninstantiated); e.g. `atl/geometry/predicates.hpp:371`: `return detail::sign_of(detail::cross_determinant(a, b, c, d));` |
| `atl/graph/concepts.hpp:249` `atl::graph::weight` | 15 | 4,140 | 3,930 | coincidental member/local/parameter; requires-clause; kept structural/declaration text; kept body/signature (including uninstantiated); e.g. `atl/graph/concepts.hpp:25`: `Weight weight;` |
| `atl/geometry/polygon.hpp:386` `atl::geometry::contains` | 8 | 4,040 | 3,896 | same-name declaration/overload; kept body/signature (including uninstantiated); coincidental member/local/parameter; e.g. `atl/geometry/segment.hpp:161`: `[[nodiscard]] constexpr bool contains(segment<T> const s, point<T> const q, Policy const p = {}) {` |
| `atl/nt/modular.hpp:23` `atl::add_mod_unchecked` | 21 | 3,969 | 3,759 | kept body/signature (including uninstantiated); e.g. `atl/nt/modular.hpp:68`: `if (b & 1) result = add_mod_unchecked(result, a, m);` |
| `atl/geometry/polygon.hpp:357` `atl::geometry::contains` | 5 | 3,920 | 3,655 | same-name declaration/overload; kept body/signature (including uninstantiated); coincidental member/local/parameter; e.g. `atl/geometry/segment.hpp:161`: `[[nodiscard]] constexpr bool contains(segment<T> const s, point<T> const q, Policy const p = {}) {` |
| `atl/graph/concepts.hpp:209` `atl::graph::target` | 4 | 2,900 | 2,768 | requires-clause; coincidental member/local/parameter; kept body/signature (including uninstantiated); e.g. `atl/graph/concepts.hpp:144`: `{ graph_traits<Edge>::target(e) } -> std::convertible_to<vertex_id>;` |
| `atl/geometry/predicates.hpp:579` `atl::geometry::orientation` | 14 | 2,898 | 2,842 | coincidental member/local/parameter; kept body/signature (including uninstantiated); requires-clause; macro; e.g. `atl/geometry/predicates.hpp:351`: `[[nodiscard]] constexpr int orientation(point<T> const o, point<T> const a, point<T> const b) const {` |
| `atl/geometry/line.hpp:339` `atl::geometry::intersection` | 3 | 2,751 | 2,481 | same-name declaration/overload; kept body/signature (including uninstantiated); e.g. `atl/geometry/line.hpp:279`: `[[nodiscard]] constexpr segment_intersection<T> intersection(segment<T> const s, segment<T> const t,` |
| `atl/nt/gcd.hpp:41` `atl::extended_gcd` | 6 | 2,748 | 2,430 | same-name declaration/overload; kept body/signature (including uninstantiated); e.g. `atl/nt/gcd.hpp:81`: `constexpr gcd_result<traits::make_unsigned_t<S>> extended_gcd(S const &a, S const &b) {` |
| `atl/ds/coordinate_compression.hpp:78` `atl::detail::ranks_by_sorted_merge_suffix` | 2 | 2,726 | 2,546 | kept body/signature (including uninstantiated); e.g. `atl/ds/coordinate_compression.hpp:290`: `detail::ranks_by_sorted_merge_suffix(values_, comp_, rest, out);` |

## G4: top 15 written declarations

| Source declaration | Occurrences | nlm B | compact B | Current reason / external witness |
| --- | ---: | ---: | ---: | --- |
| `atl/nt/detail/uint128_stream.hpp:105` `atl::detail::read_stream_magnitude` | 18 | 43,650 | 39,798 | kept body/signature (including uninstantiated); e.g. `atl/nt/mod_int.hpp:166`: `auto const parsed = detail::read_stream_magnitude<std::conditional_t<(sizeof(T) > sizeof(uint64)), uint128, uint64>>(is);` |
| `atl/nt/matrix.hpp:185` `atl::detail::lazy_product` | 1 | 1,801 | 1,639 | kept body/signature (including uninstantiated); e.g. `atl/nt/matrix.hpp:368`: `c.entries_ = detail::lazy_product<T>(lhs.entries_, rhs.entries_, lhs.rows_, lhs.columns_,` |

## blocked c/d: top 15 written declarations

| Source declaration | Occurrences | nlm B | compact B | Current reason / external witness |
| --- | ---: | ---: | ---: | --- |
| `atl/nt/gcd.hpp:41` `atl::extended_gcd` | 1 | 458 | 405 | family aggregation ← atl::extended_gcd (1) |
| `atl/nt/mod_int.hpp:165` `atl::operator>>` | 1 | 414 | 389 | family aggregation ← atl::dynamic_mod_int (1) |
| `atl/nt/power.hpp:32` `atl::power` | 1 | 397 | 356 | instantiation effect: possible trigger of atl::dynamic_mod_int ← atl::dynamic_mod_int (1) |
| `atl/nt/gcd.hpp:81` `atl::extended_gcd` | 1 | 390 | 366 | instantiation effect: possible trigger of atl::dynamic_mod_int ← atl::dynamic_mod_int (1) |
| `atl/nt/dynamic_mod_int.hpp:234` `atl::operator>>` | 1 | 371 | 346 | never removed: operator (1) |
| `atl/nt/mod_int.hpp:148` `atl::operator<<` | 1 | 343 | 322 | family aggregation ← atl::dynamic_mod_int (1) |
| `atl/nt/modular.hpp:297` `atl::pow_mod` | 1 | 304 | 281 | instantiation effect: possible trigger of atl::dynamic_mod_int ← atl::dynamic_mod_int (1) |
| `atl/nt/modular.hpp:91` `atl::mul_mod_unchecked` | 1 | 300 | 279 | instantiation effect: possible trigger of atl::dynamic_mod_int ← atl::dynamic_mod_int (1) |
| `atl/nt/power.hpp:65` `atl::power` | 1 | 276 | 256 | family aggregation ← atl::power (1) |
| `atl/nt/modular.hpp:367` `atl::inverse_mod` | 1 | 185 | 175 | instantiation effect: possible trigger of atl::dynamic_mod_int ← atl::dynamic_mod_int (1) |
| `atl/nt/modular.hpp:41` `atl::sub_mod_unchecked` | 1 | 173 | 165 | instantiation effect: possible trigger of atl::dynamic_mod_int ← atl::dynamic_mod_int (1) |
| `atl/nt/modular.hpp:243` `atl::add_mod` | 1 | 162 | 154 | instantiation effect: possible trigger of atl::dynamic_mod_int ← atl::dynamic_mod_int (1) |
| `atl/nt/modular.hpp:260` `atl::sub_mod` | 1 | 162 | 154 | instantiation effect: possible trigger of atl::dynamic_mod_int ← atl::dynamic_mod_int (1) |
| `atl/nt/modular.hpp:277` `atl::mul_mod` | 1 | 162 | 154 | instantiation effect: possible trigger of atl::dynamic_mod_int ← atl::dynamic_mod_int (1) |
| `atl/nt/modular.hpp:335` `atl::decr_mod` | 1 | 139 | 131 | instantiation effect: possible trigger of atl::dynamic_mod_int ← atl::dynamic_mod_int (1) |

## G1 current-rule totals

These are mutually exclusive first retention reasons, not extra gains. `Name rule` is production lexical closure; association is the explicit keep rule 3, under the requested “other” category.

| Current production rule | Declarations | nlm B | compact B |
| --- | ---: | ---: | ---: |
| Name rule / lexical closure | 32 | 21,833 | 19,846 |
| Family aggregation | 32 | 16,404 | 15,405 |
| Other: signature/type association (rule 3) | 114 | 110,622 | 102,301 |
| AST closure | 0 | 0 | 0 |
| Dependent-name closure | 0 | 0 | 0 |
| A6 | 0 | 0 | 0 |
| Potential effects | 0 | 0 | 0 |
| Root/pin | 0 | 0 | 0 |

## G1: why every declaration remains today

The recorded reason is production’s first retention cause; aggregation and structural ownership can supply additional independent obligations. This complete source-declaration inventory groups repeated example occurrences.

| Declaration | Occurrences | nlm B | compact B | Production rule | Exact first reason |
| --- | ---: | ---: | ---: | --- | --- |
| `atl/graph/traversal.hpp:1007` `atl::graph::dfs_fold` | 12 | 20,556 | 19,068 | association rule | associated with atl::graph::visit ← atl::graph::visit (12) |
| `atl/nt/basic.hpp:137` `atl::factorization` | 12 | 12,396 | 10,944 | association rule | associated with atl::prime_factor ← atl::prime_factor (12) |
| `atl/utils/counting_sort.hpp:108` `atl::detail::counting_permutation` | 9 | 9,639 | 8,712 | association rule | associated with atl::detail::counting_sort_scratch ← atl::detail::counting_sort_scratch (9) |
| `atl/geometry/polygon.hpp:258` `atl::geometry::winding_of` | 6 | 7,032 | 6,816 | association rule | associated with atl::geometry::winding ← atl::geometry::winding (6) |
| `atl/geometry/polygon.hpp:218` `atl::geometry::polygon_centroid` | 7 | 6,818 | 6,447 | association rule | associated with atl::geometry::point ← atl::geometry::point (7) |
| `atl/geometry/polygon.hpp:66` `atl::geometry::convexity_of` | 4 | 6,364 | 5,864 | association rule | associated with atl::geometry::convexity ← atl::geometry::convexity (4) |
| `atl/geometry/convex_polygon_operations.hpp:576` `atl::geometry::union_of_convex_polygons` | 1 | 5,762 | 5,225 | association rule | associated with atl::geometry::point ← atl::geometry::point (1) |
| `atl/geometry/convex_polygon_operations.hpp:750` `atl::geometry::closest_points_between_convex_hulls` | 2 | 4,304 | 4,008 | association rule | associated with atl::geometry::point ← atl::geometry::point (2) |
| `atl/graph/traversal.hpp:1050` `atl::graph::dfs_fold` | 12 | 3,984 | 3,912 | family rule | family aggregation ← atl::graph::dfs_fold (12) |
| `atl/geometry/point.hpp:365` `atl::geometry::detail::float64_difference` | 10 | 3,860 | 3,610 | association rule | associated with atl::geometry::point ← atl::geometry::point (10) |
| `atl/geometry/circle.hpp:531` `atl::geometry::minimum_enclosing_circle` | 2 | 3,250 | 2,914 | association rule | associated with atl::geometry::circle ← atl::geometry::circle (2) |
| `atl/geometry/line.hpp:279` `atl::geometry::intersection` | 2 | 3,098 | 2,790 | family rule | family aggregation ← atl::geometry::intersection (2) |
| `atl/geometry/convex_hull.hpp:97` `atl::geometry::detail::prune_octagon` | 1 | 3,081 | 2,775 | name rule | name: prune_octagon ← atl::geometry::detail::hull_indices (1) |
| `atl/geometry/circle.hpp:394` `atl::geometry::common_tangents` | 2 | 2,950 | 2,698 | association rule | associated with atl::static_vector ← atl::static_vector (2) |
| `atl/geometry/line.hpp:172` `atl::geometry::line_from_coefficients` | 2 | 2,890 | 2,810 | association rule | associated with atl::geometry::line ← atl::geometry::line (2) |
| `atl/geometry/delaunay.hpp:853` `atl::geometry::largest_empty_circle` | 1 | 2,814 | 2,581 | association rule | associated with atl::geometry::circle ← atl::geometry::circle (1) |
| `atl/geometry/convex_polygon_operations.hpp:494` `atl::geometry::intersection_of_convex_polygons` | 1 | 2,471 | 2,295 | association rule | associated with atl::geometry::point ← atl::geometry::point (1) |
| `atl/geometry/point_location.hpp:570` `atl::geometry::locations_of` | 1 | 2,431 | 2,204 | association rule; local D3e rule 1/family ownership (309 body B) | associated with atl::geometry::location ← atl::geometry::location (1) |
| `atl/geometry/circle.hpp:242` `atl::geometry::intersection` | 1 | 1,996 | 1,823 | family rule | family aggregation ← atl::geometry::intersection (1) |
| `atl/nt/sieve.hpp:93` `atl::sieve_info` | 15 | 1,995 | 1,950 | association rule | associated with atl::sieve_item_info ← atl::sieve_item_info (15) |
| `atl/geometry/convex_polygon_operations.hpp:267` `atl::geometry::detail::boundary_contact` | 1 | 1,850 | 1,606 | name rule | name: boundary_contact ← atl::geometry::intersection_of_convex_polygons (1) |
| `atl/geometry/circle.hpp:353` `atl::geometry::tangent_points` | 2 | 1,842 | 1,710 | association rule | associated with atl::static_vector ← atl::static_vector (2) |
| `atl/geometry/line.hpp:339` `atl::geometry::intersection` | 2 | 1,834 | 1,654 | family rule | family aggregation ← atl::geometry::intersection (2) |
| `atl/graph/bipartite_matching.hpp:584` `atl::graph::matching_edge_roles` | 1 | 1,795 | 1,634 | association rule | associated with atl::graph::matching_edge ← atl::graph::matching_edge (1) |
| `atl/geometry/convex_polygon_operations.hpp:214` `atl::geometry::detail::halfplane_intersection` | 1 | 1,634 | 1,429 | name rule | name: halfplane_intersection ← atl::geometry::intersection_of_convex_polygons (1) |
| `atl/geometry/line.hpp:425` `atl::geometry::intersection` | 2 | 1,588 | 1,516 | family rule; association rule | family aggregation ← atl::geometry::intersection (1); associated with atl::geometry::point ← atl::geometry::point (1) |
| `atl/geometry/convex_hull.hpp:46` `atl::geometry::detail::chain_hull` | 1 | 1,485 | 1,346 | name rule | name: chain_hull ← atl::geometry::detail::hull_indices (1) |
| `atl/geometry/calipers.hpp:550` `atl::geometry::detail::difference_of` | 1 | 1,474 | 1,334 | name rule | name: difference_of ← atl::geometry::intersection_of_convex_polygons (1) |
| `atl/geometry/segment_intersections.hpp:564` `atl::geometry::segment_intersections` | 3 | 1,473 | 1,326 | association rule | associated with atl::geometry::materialized_intersection ← atl::geometry::materialized_intersection (3) |
| `atl/geometry/line.hpp:401` `atl::geometry::projection` | 4 | 1,472 | 1,432 | association rule | associated with atl::geometry::point ← atl::geometry::point (4) |
| `atl/geometry/convex_polygon_operations.hpp:170` `atl::geometry::detail::merged_halfplanes` | 1 | 1,391 | 1,228 | name rule | name: merged_halfplanes ← atl::geometry::intersection_of_convex_polygons (1) |
| `atl/geometry/point.hpp:243` `atl::geometry::detail::check_floating_magnitude` | 5 | 1,365 | 1,275 | name rule | name: check_floating_magnitude ← atl::geometry::polygon_centroid (5) |
| `atl/geometry/convex_hull.hpp:301` `atl::geometry::convex_contains` | 1 | 1,326 | 1,237 | association rule | associated with atl::geometry::containment ← atl::geometry::containment (1) |
| `atl/geometry/predicates.hpp:557` `atl::geometry::detail::sweep_before` | 4 | 1,248 | 1,160 | name rule | name: sweep_before ← atl::geometry::convexity_of (4) |
| `atl/geometry/convex_polygon_operations.hpp:117` `atl::geometry::detail::crossing_side` | 1 | 1,102 | 997 | name rule | name: crossing_side ← atl::geometry::detail::halfplane_intersection (1) |
| `atl/geometry/circle.hpp:98` `atl::geometry::relation` | 1 | 1,057 | 1,010 | name rule | name: relation ← atl::geometry::intersection (1) |
| `atl/geometry/calipers.hpp:732` `atl::geometry::min_perimeter_enclosing_rectangle` | 3 | 1,044 | 1,020 | association rule | associated with atl::geometry::point ← atl::geometry::point (3) |
| `atl/geometry/convex_hull.hpp:185` `atl::geometry::detail::hull_indices` | 1 | 1,035 | 948 | name rule | name: hull_indices ← atl::geometry::convex_hull_indices (1) |
| `atl/geometry/circle.hpp:316` `atl::geometry::intersection` | 1 | 1,024 | 964 | family rule | family aggregation ← atl::geometry::intersection (1) |
| `atl/geometry/calipers.hpp:714` `atl::geometry::min_area_enclosing_rectangle` | 3 | 1,014 | 990 | association rule | associated with atl::geometry::point ← atl::geometry::point (3) |
| `atl/geometry/circle.hpp:164` `atl::geometry::relation` | 1 | 977 | 920 | family rule | family aggregation ← atl::geometry::relation (1) |
| `atl/geometry/circle.hpp:198` `atl::geometry::intersection` | 1 | 958 | 909 | association rule | associated with atl::static_vector ← atl::static_vector (1) |
| `atl/geometry/convex_polygon_operations.hpp:433` `atl::geometry::detail::append_arc` | 1 | 911 | 816 | association rule | associated with atl::geometry::point ← atl::geometry::point (1) |
| `atl/graph/bipartite_matching.hpp:295` `atl::graph::minimum_vertex_cover` | 1 | 907 | 854 | association rule | associated with atl::graph::bipartite_matching_result ← atl::graph::bipartite_matching_result (1) |
| `atl/geometry/predicates.hpp:545` `atl::geometry::detail::sweep_group` | 4 | 804 | 720 | name rule | name: sweep_group ← atl::geometry::detail::sweep_before (4) |
| `atl/geometry/segment.hpp:177` `atl::geometry::intersects` | 1 | 696 | 660 | name rule | name: intersects ← atl::geometry::intersection (1) |
| `atl/geometry/convex_polygon_operations.hpp:377` `atl::geometry::detail::param_before` | 1 | 624 | 558 | name rule | name: param_before ← atl::geometry::union_of_convex_polygons (1) |
| `atl/geometry/convex_polygon_operations.hpp:81` `atl::geometry::detail::check_operation_bounds` | 1 | 619 | 561 | name rule | name: check_operation_bounds ← atl::geometry::intersection_of_convex_polygons (1) |
| `atl/geometry/line.hpp:144` `atl::geometry::line_coefficients` | 1 | 512 | 494 | name rule | name: line_coefficients ← atl::geometry::detail::crossing_side (1) |
| `atl/graph/bipartite_matching.hpp:326` `atl::graph::maximum_independent_set` | 1 | 474 | 441 | association rule | associated with atl::graph::bipartite_matching_result ← atl::graph::bipartite_matching_result (1) |
| `atl/geometry/convex_polygon_operations.hpp:414` `atl::geometry::detail::boundary_position` | 1 | 456 | 410 | name rule | name: boundary_position ← atl::geometry::detail::append_arc (1) |
| `atl/geometry/convex_hull.hpp:273` `atl::geometry::convex_hull` | 1 | 454 | 421 | family rule | family aggregation ← atl::geometry::convex_hull (1) |
| `atl/geometry/line.hpp:372` `atl::geometry::intersection` | 2 | 454 | 412 | family rule | family aggregation ← atl::geometry::intersection (2) |
| `atl/geometry/convex_hull.hpp:259` `atl::geometry::convex_hull` | 1 | 397 | 387 | name rule | name: convex_hull ← atl::geometry::min_area_enclosing_rectangle (1) |
| `atl/geometry/convex_polygon_operations.hpp:401` `atl::geometry::detail::overlap_of` | 1 | 380 | 355 | name rule | name: overlap_of ← atl::geometry::intersection_of_convex_polygons (1) |
| `atl/geometry/line.hpp:194` `atl::geometry::line_from_coefficients` | 2 | 364 | 356 | family rule | family aggregation ← atl::geometry::line_from_coefficients (2) |
| `atl/geometry/line.hpp:112` `atl::geometry::intersects` | 1 | 287 | 281 | family rule | family aggregation ← atl::geometry::intersects (1) |
| `atl/geometry/convex_hull.hpp:247` `atl::geometry::convex_hull_indices` | 1 | 278 | 254 | association rule | associated with atl::geometry::keep_collinear_t ← atl::geometry::keep_collinear_t (1) |
| `atl/geometry/point.hpp:385` `atl::geometry::to_float64` | 2 | 270 | 262 | association rule | associated with atl::geometry::point ← atl::geometry::point (2) |
| `atl/graph/functional_graph.hpp:427` `atl::graph::functional_graph_decomposition_of` | 1 | 269 | 265 | association rule | associated with atl::graph::functional_graph_decomposition ← atl::graph::functional_graph_decomposition (1) |
| `atl/geometry/convex_polygon_operations.hpp:320` `atl::geometry::detail::as_float_polygon` | 1 | 263 | 247 | association rule | associated with atl::geometry::point ← atl::geometry::point (1) |
| `atl/geometry/convex_polygon_operations.hpp:157` `atl::geometry::detail::rotate_hull_to_low` | 1 | 262 | 245 | name rule | name: rotate_hull_to_low ← atl::geometry::intersection_of_convex_polygons (1) |
| `atl/geometry/line.hpp:80` `atl::geometry::parallel` | 1 | 240 | 234 | name rule | name: parallel ← atl::geometry::intersects (1) |
| `atl/geometry/convex_hull.hpp:232` `atl::geometry::convex_hull_indices` | 1 | 239 | 235 | family rule | family aggregation ← atl::geometry::convex_hull_indices (1) |
| `atl/geometry/line.hpp:101` `atl::geometry::intersects` | 1 | 217 | 213 | family rule | family aggregation ← atl::geometry::intersects (1) |
| `atl/geometry/line.hpp:119` `atl::geometry::intersects` | 1 | 187 | 183 | family rule | family aggregation ← atl::geometry::intersects (1) |
| `atl/geometry/circle.hpp:185` `atl::geometry::relation` | 1 | 186 | 182 | family rule | family aggregation ← atl::geometry::relation (1) |
| `atl/geometry/circle.hpp:302` `atl::geometry::intersection` | 1 | 156 | 152 | family rule | family aggregation ← atl::geometry::intersection (1) |
| `atl/geometry/circle.hpp:225` `atl::geometry::intersection` | 1 | 153 | 149 | family rule | family aggregation ← atl::geometry::intersection (1) |
| `atl/geometry/point.hpp:252` `atl::geometry::detail::widened_difference` | 1 | 121 | 114 | name rule | name: widened_difference ← atl::geometry::detail::float64_difference (1) |

## Hazards and validation

No concrete ATL example was found in which a G1 declaration remains discoverable after the complete group is removed. The important distinction from the shelved overload experiment is clause (b): an unused overload **stays** whenever the spelling appears anywhere else, even when a failed requires-expression or ambiguity produces no resolved reference. Operators and conversion/deduction protocols are excluded outright. This is an audit under the repository's existing assumptions, not a proof for arbitrary unregistered libraries or preprocessing configurations.

Concrete boundaries checked in ATL:

- `atl::factorization` is G1 only in outputs with no remaining `factorization` token. `docs/examples/factorization/quick.cpp:7` directly calls it; that occurrence is excluded from the pool by references/flags. Likewise, `docs/examples/graph/callbacks.cpp:14` names `dfs_fold`; unused `dfs_fold` overloads in other examples disappear together only when the entire spelling is absent.
- `counting_permutation` (`utils/counting_sort.hpp:108`) is kept through its scratch-type association even when no kept sort API names it. Its body calls `bucket_by_key`, but it does not call the adjacent `counting_sort_buffers` with block-scope thread storage. Deleting it therefore does not delete an independently rooted storage owner.
- `convolution_uint64` illustrates the rejected per-overload approach: when a retained overload or caller still spells that name, the unreferenced overload is G3. The same happens to geometry `intersection` overloads in outputs that use that spelling. No worker viability decision is treated as an all-target proof.
- `hash_float_value` (`utils/hash.hpp:270`) remains in G3 because the retained floating-hash `operator()` names it at line 309 inside an `if constexpr` fallback. That template text must remain valid for another floating representation even when the worker never instantiates the branch.
- Macro-reached names do occur in G3: `graph::detail::fits_vertex_id` (`graph/concepts.hpp:192`) is present in `atl_assert` arguments at lines 211/215, and `detail::sub_overflows` (`types/detail/checked_arithmetic.hpp:28`) is present in the assertion at `ds/linear_function.hpp:103`. Raw arguments and expanded tokens protect them. The constraint at `graph/concepts.hpp:144` spells `graph_traits<Edge>::target(e)`, protecting the free `target` spelling even when the binding is elsewhere.
- `graph::weight` (`graph/concepts.hpp:249`) is blocked by the data-member spelling `Weight weight` at line 25 as well as requires-expression uses. Geometry's `orientation` also collides with policy members and constraints. These coincidental identifiers intentionally satisfy (b), even when they do not bind to the free function.
- `mod_int::operator>>` (`nt/mod_int.hpp:165`) still spells `read_stream_magnitude` at line 166. The matrix `operator*` (`nt/matrix.hpp:361`) still spells `lazy_product` at line 368. These are G4 helpers of G2 operators; an uninstantiated retained body still needs valid lookup. A different target/library can instantiate those protocols, which is why neither owner nor helper is included in G1.

Raw tokens and the user preprocessed stream were checked separately. A macro body or argument that spells a name is a raw witness; a pasted name is a preprocessed witness at its expansion site. The existing external-paste/macro-agreement gates remain in force. A using-declaration, an out-of-group friend declaration, a default argument, a template argument, an address expression or a callback registration that names a candidate likewise supplies a raw/expanded witness; a namespace using-directive still needs a later lookup spelling, or an excluded language/library protocol. A function pointer passed into a library has an AST edge and ordinarily an address/name origin as well. No ATL dynamic-symbol/string lookup mechanism (`dlsym`, `dlopen`, `GetProcAddress`) or assembly was found in the supplied headers/examples. The final audit also searches the retained candidate output for candidate spellings inside strings, beyond the token rule.

`modular-arithmetic-residues` supplies a concrete (d) rejection: `dynamic_mod_int` has template static `reducer_` storage (`nt/dynamic_mod_int.hpp:45`). The production reverse graph marks possible triggers including `add_mod`, `sub_mod`, `mul_mod`, `pow_mod` and `mul_mod_unchecked`; some have no remaining external name. The analysis preserves this conservative graph barrier rather than treating absence of a direct worker call as an effect proof. This is why a separate c/d row is necessary.

The production potential-effect traversal is preserved and recorded, including graph visits whose first retention reason was already something else. A11 still bounds friend injection and instantiation/destruction effects; the analysis does not silently waive it. There are no ordinary hidden-friend G1 cases in the measured corpus, so this result provides no empirical need to split such friends from their structural records. A future implementation should retain all existing root/pin and effect barriers, and treat hidden-friend signature instantiation as a separate review surface if that scope is added.

The changes suggested by the G1 evidence are to signature/type association (keep rule 3), family aggregation, and the lexical closure they seed. The full inventory records each immediate cause, and the raw JSON stores its complete retention chain. There is no measured G1 gain from weakening A6, free-operator protection, root/pin rules or potential-effect handling. A namespace-function implementation would realize the measured gain without changing the D3e ownership of hidden friends.


All 126 baseline artifacts prune and pass standalone sample output. Production whitespace compaction and token/directive invariants pass for all 126 measured artifacts. G1 deletion artifacts: **17/17** selected example checks pass, covering **70/70** distinct written G1 declarations (the 15 largest example groups plus additional source-declaration coverage). The compacted deletion artifacts compile at `-O2` and match samples; no claim of every target/library instantiation is made. Unmodified-production-extractor replay: **126/126** model comparisons pass after sorting/deduplicating unordered graph edges; unordered-edge order does not change retention. Six focused CTest suites pass, as listed above.

Raw per-example declarations, complete retention chains, raw/preprocessed witnesses, actual effect visits, rendered/compact copy maps, counterfactuals and check results remain in ignored `measurements/trim/`. The diagnostic patch and reproduction commands are in [free-analysis-diag.patch](free-analysis-diag.patch). Production sources are restored; SDK archive/extraction deleted. Only this review and that patch remain as git changes; nothing was committed or pushed.

## Verdict

The strict candidate rule removes **148,859 owned nlm bytes (3.282% of 4,535,428 output bytes)** in the 126 analyzed examples. It looks worth building as a narrow namespace-function refinement: all measured G1 gain comes from namespace functions and their internal bodies, and the largest example (`delaunay-quick`) loses 48,861 owned bytes. The main change is to type association and the family/lexical closure it seeds; this corpus supplies no reason to weaken A6, non-member operator protection or potential-effect barriers, and no additional gain from ordinary hidden friends.
