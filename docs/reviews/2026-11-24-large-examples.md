<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Large ATL examples: byte ownership, dormant code, and library splits

2026-11-24. **Analysis only; no production change, commit, or push.** Inputs: the thirteen requested compact outputs and reports in `caide-compare-shard-*/examples/`, and the supplied ATL checkout `5b14f2d`. The opening instruction to leave work uncommitted takes precedence over the contradictory closing commit request. Work was performed in the supplied worktree; external inputs were read only.

**The thirteen compact outputs total 1,074,486 B. Name retention owns 747,735 B (69.59%).** The most useful shared library changes are optional modular-integer streaming, separating primality from sieve/container dependencies, and separating native 32-bit series products from wide-integer products. The largest single dormant cluster is the link-cut example's unused edge registry and hash containers: **18,578 B** can be replaced by an incomplete type declaration in the measured prototype.

Using the requested percentages sequentially, **3/13** already fit. The combined split/policy scenarios below project **12/13** below 65,536 B; fast series composition remains **76,173 B**. An explicitly elementary API for that precision-five example produces a **32,461 B** readable prototype, making **13/13 plausible if changing which algorithm that example demonstrates is acceptable**. Preserving the fast composition demonstration leaves one unresolved size problem. These are measured local prototype sizes plus estimated shortening, not newly generated production distill outputs.

## Inputs, limit model, and comparison caveat

Every supplied report selects **`cses-gcc13-cpp23`**, D3e with D3f, A11, A12 and A6/A7. These are not Codeforces-profile artifacts. The target here is source size; no Codeforces configuration certificate is inferred from them.

All **13 caide `verify.rep` files report fallback to unoptimized output after an embedded Clang check failed**. Their declaration rows describe an attempted optimization, not the declarations actually written in `verify.cpp`. Consequently the caide column below is a size comparison only, never an oracle proving a distill-kept declaration dead. The previous review's 100-example selection explicitly excluded these fallback cases; applying that selection here would discard the entire requested population.

For compact size `C` and a measured prototype saving `S`, use:

```text
projected bytes = ceil((C - S) * 0.91 * 0.96)
                = ceil((C - S) * 8736 / 10000)
fit             = projected bytes < 65536
```

Thus the largest acceptable remaining compact size in this conservative rounding model is **75,017 B**. Percentages are forecasts; a seven-byte projected miss is not a reliable empirical margin. Interpreting the percentages additively as 13% would instead put five original examples under the limit, because link-cut and polygon-overlay cross the boundary. All counts in this review use the explicitly stated sequential model consistently.

<!-- BEGIN baseline -->
| Example | Compact B | Projected B | Additional compact B to remove | Caide verify B (fallback) |
| --- | --- | --- | --- | --- |
| series-composition-quick | 110,670 | 96,682 | 35,653 | 172,691 |
| polynomial-points-quick | 92,407 | 80,727 | 17,390 | 145,556 |
| power-series-powers-and-roots | 88,938 | 77,697 | 13,921 | 117,062 |
| polynomial-gcd-quick | 87,739 | 76,649 | 12,722 | 159,375 |
| polynomial-gcd-pade | 85,976 | 75,109 | 10,959 | 159,175 |
| point-location-quick | 84,012 | 73,393 | 8,995 | 113,153 |
| linear-recurrence-quick | 75,796 | 66,216 | 779 | 105,580 |
| polynomial-points-sampling | 77,988 | 68,131 | 2,971 | 145,459 |
| link-cut-tree-basic | 75,025 | 65,542 | 8 | 100,459 |
| polygon-overlay-quick | 75,060 | 65,573 | 43 | 95,192 |
| power-series-quick | 73,954 | 64,607 | 0 | 116,770 |
| online-convolution-partitions | 73,676 | 64,364 | 0 | 150,993 |
| online-convolution-quick | 73,245 | 63,987 | 0 | 150,719 |
<!-- END baseline -->

## Byte ownership by header and retaining reason

The complete [header-by-example matrix](large-examples/header-by-example.csv) gives **every one of the 66 ATL headers, the example's own code/includes, every example, and aggregate totals**. Zero cells mean no written bytes from that header. The [reason-by-example matrix](large-examples/reason-by-example.csv) gives the corresponding complete retention partition. Both matrices reconcile exactly to the thirteen actual compact files; neither is a top-N sample.

### Method and precision

The scripts reuse `norm`, declaration extraction, report parsing, and reason-group conventions from [the caide-gap analysis](2026-11-24-caide-gap-analysis.md), without importing its population filter or running its ledger. They measure the **actual compact artifacts**, not report estimates of pre-compaction source ranges.

* Parse the output with pinned tree-sitter C++ 0.23.4/runtime 0.27.0. The previous review's offset-preserving syntax views handle `if consteval` and other grammar differences. All thirteen views have zero parse errors. Map **3,566 written family occurrences** to kept report rows by normalized name and original-source order. Collapse implicit-instance duplicates; distinguish written overload occurrences by their order and source locations.
* Recover full header provenance by recursively following original ATL includes and aligning comment-stripped, edge-trimmed source lines to compact lines. Generated once guards match the corresponding original `#pragma once`. Skip the software-int128 include: every artifact explicitly defines `ATL_HAS_INT128 1`. Both conditional arms can otherwise remain in the alignment input; deleted lines simply do not match. This is provenance reconstruction, not a replacement preprocessor.
* Cross-check every reported declaration span against its recovered header. There are **zero unresolved output-line origins and zero header conflicts** in all thirteen examples. Two 49 B `segment_sweep` construction lines, one in each geometry example, differ in comment-removal spacing and receive their header from the enclosing reported `arrangement_of` family rather than exact line alignment. Every other output line aligns directly. Generic braces alone would be weak anchors; the original include order and the independent declaration locations provide the checks. Header counts charge whole physical lines, including includes, generated guards, namespace shells and newlines, to their source header. The example category includes its own system includes and `main`.
* For reasons, directives own entire physical/logical directive lines first. Otherwise the smallest independently reported written family owns a byte. A record retains its non-extracted aliases, fields, nested records and hidden friends; separately reported D3e methods own their own spans. Follow `family aggregation` to its effective immediate reason. Do not charge a helper to its eventual root as well. Remaining shells, gaps, unreported namespace assertions and CTAD guides have a separate structural category. That category contains code, especially link-cut guides; it is not all removable whitespace.

Source attribution and reason ownership are two partitions of the same bytes, not additive savings. Complete per-declaration data, including immediate reasons and retention chains, is reproduced in ignored `measurements/large-examples/declarations.csv`.

### Per-example header overview

Header paths below are relative to `atl/include/atl/`; the linked CSV expands all remaining headers individually.

<!-- BEGIN per-header -->
| Example | Own code/includes B | Three largest ATL headers (compact B) | Other ATL B |
| --- | --- | --- | --- |
| series-composition-quick | 903 | `nt/detail/series_composition.hpp` 16,487; `nt/convolution.hpp` 15,406; `nt/detail/power_series.hpp` 9,898 | 67,976 |
| polynomial-points-quick | 936 | `nt/detail/polynomial_points.hpp` 15,987; `nt/convolution.hpp` 15,406; `nt/ntt_engine.hpp` 6,591 | 53,487 |
| power-series-powers-and-roots | 1,028 | `nt/convolution.hpp` 15,406; `nt/detail/power_series.hpp` 9,898; `nt/detail/power_series_extensions.hpp` 9,204 | 53,402 |
| polynomial-gcd-quick | 943 | `nt/convolution.hpp` 15,406; `nt/detail/polynomial_gcd.hpp` 11,383; `nt/ntt_engine.hpp` 6,591 | 53,416 |
| polynomial-gcd-pade | 757 | `nt/convolution.hpp` 15,406; `nt/detail/polynomial_gcd.hpp` 11,383; `nt/ntt_engine.hpp` 6,591 | 51,839 |
| point-location-quick | 573 | `geometry/point_location.hpp` 11,270; `geometry/segment_intersections.hpp` 10,781; `geometry/predicates.hpp` 9,117 | 52,271 |
| linear-recurrence-quick | 572 | `nt/convolution.hpp` 12,030; `nt/linear_recurrence.hpp` 7,834; `nt/matrix.hpp` 7,659 | 47,701 |
| polynomial-points-sampling | 847 | `nt/convolution.hpp` 15,406; `nt/detail/polynomial_points.hpp` 9,858; `nt/ntt_engine.hpp` 6,591 | 45,286 |
| link-cut-tree-basic | 448 | `ds/link_cut_tree.hpp` 33,053; `ds/detail/hash_table.hpp` 8,207; `ds/flat_hash_map.hpp` 8,115 | 25,202 |
| polygon-overlay-quick | 490 | `geometry/segment_intersections.hpp` 11,990; `geometry/predicates.hpp` 9,117; `ds/detail/hash_table.hpp` 8,207 | 45,256 |
| power-series-quick | 743 | `nt/convolution.hpp` 15,406; `nt/detail/power_series.hpp` 9,258; `nt/ntt_engine.hpp` 6,591 | 41,956 |
| online-convolution-partitions | 701 | `nt/convolution.hpp` 15,406; `nt/detail/online_convolution.hpp` 7,876; `nt/ntt_engine.hpp` 6,591 | 43,102 |
| online-convolution-quick | 436 | `nt/convolution.hpp` 15,406; `nt/detail/online_convolution.hpp` 7,876; `nt/ntt_engine.hpp` 6,591 | 42,936 |
<!-- END per-header -->

### Aggregate compact bytes by immediate reason

<!-- BEGIN reasons -->
| Immediate reason group | Compact B | Percent |
| --- | --- | --- |
| name rule | 747,735 | 69.59% |
| A12 library-spelled member | 85,114 | 7.92% |
| AST | 58,703 | 5.46% |
| D3e rule 1 kinds | 47,385 | 4.41% |
| roots/effects | 36,994 | 3.44% |
| includes | 31,878 | 2.97% |
| never-removed operator | 31,525 | 2.93% |
| structural trivia/shells/unreported | 16,114 | 1.50% |
| generated guards | 14,820 | 1.38% |
| other directives | 3,226 | 0.30% |
| pinned | 870 | 0.08% |
| dependent name | 122 | 0.01% |
<!-- END reasons -->

Association and A6/ADL have no separate immediate-reason bytes here. That does not mean those protections are dispensable: hidden operators are often charged to their enclosing record. `roots/effects` includes namespace/static-storage roots, `main`, assertions and D3e rule-7 roots. `pinned` is the wide-arithmetic macro-invocation flag. The two dependent-name occurrences are `hash_table::end`, **61 B each**, in the geometry examples; that spelling is an iterator protocol actually used there.

### Aggregate compact bytes by header, complete

<!-- BEGIN headers -->
| Header | N examples | Compact B |
| --- | --- | --- |
| atl/nt/convolution.hpp | 10 | 150,684 |
| atl/nt/ntt_engine.hpp | 10 | 65,910 |
| atl/nt/detail/uint128_stream.hpp | 10 | 53,160 |
| atl/nt/detail/power_series.hpp | 9 | 52,904 |
| atl/nt/montgomery.hpp | 10 | 51,380 |
| atl/types/traits/integral.hpp | 13 | 37,512 |
| atl/ds/static_vector.hpp | 10 | 35,500 |
| atl/nt/detail/polynomial_points.hpp | 7 | 34,595 |
| atl/ds/link_cut_tree.hpp | 1 | 33,053 |
| atl/nt/mod_int.hpp | 10 | 30,850 |
| atl/types/detail/wide_arithmetic.hpp | 10 | 29,690 |
| atl/nt/static_modulus.hpp | 10 | 25,910 |
| atl/ds/detail/hash_table.hpp | 3 | 24,621 |
| atl/utils/hash.hpp | 3 | 23,754 |
| atl/ds/flat_hash_map.hpp | 3 | 22,939 |
| atl/geometry/segment_intersections.hpp | 2 | 22,771 |
| atl/nt/detail/polynomial_gcd.hpp | 2 | 22,766 |
| atl/nt/detail/power_series_extensions.hpp | 9 | 21,537 |
| atl/nt/fft.hpp | 10 | 20,360 |
| atl/types/traits/widen_mul.hpp | 12 | 18,834 |
| atl/geometry/predicates.hpp | 2 | 18,234 |
| atl/nt/modular.hpp | 10 | 17,460 |
| atl/nt/power_series.hpp | 9 | 16,514 |
| atl/nt/detail/series_composition.hpp | 1 | 16,487 |
| atl/geometry/arrangement.hpp | 2 | 16,104 |
| atl/nt/detail/online_convolution.hpp | 2 | 15,752 |
| atl/ds/detail/forward_element.hpp | 13 | 11,817 |
| atl/nt/basic.hpp | 10 | 11,790 |
| atl/types/concepts.hpp | 13 | 11,565 |
| atl/geometry/point_location.hpp | 1 | 11,270 |
| atl/nt/gcd.hpp | 10 | 9,450 |
| example | 13 | 9,377 |
| atl/geometry/point.hpp | 2 | 8,466 |
| atl/nt/linear_recurrence.hpp | 1 | 7,834 |
| atl/ds/splay_engine.hpp | 1 | 7,732 |
| atl/nt/matrix.hpp | 1 | 7,659 |
| atl/nt/power.hpp | 10 | 7,090 |
| atl/nt/factorial_table.hpp | 9 | 6,782 |
| atl/nt/polynomial_gcd.hpp | 2 | 6,271 |
| atl/nt/series_calculus.hpp | 9 | 5,538 |
| atl/nt/prime_mod_int.hpp | 9 | 5,310 |
| atl/nt/detail/series_composition_extensions.hpp | 1 | 5,231 |
| atl/geometry/polygon_overlay.hpp | 1 | 5,210 |
| atl/nt/sieve.hpp | 10 | 4,470 |
| atl/types/detail/int256.hpp | 2 | 4,080 |
| atl/types/alias.hpp | 13 | 4,035 |
| atl/types/traits/is_one_of.hpp | 13 | 3,965 |
| atl/nt/polynomial_points.hpp | 3 | 3,715 |
| atl/nt/gaussian_elimination.hpp | 1 | 3,655 |
| atl/geometry/segment.hpp | 2 | 3,522 |
| atl/ds/detail/treap_base.hpp | 1 | 3,511 |
| atl/nt/square_root_mod.hpp | 9 | 2,942 |
| atl/types/traits/detail/extended_integer.hpp | 13 | 2,678 |
| atl/utils/assert.hpp | 13 | 2,626 |
| atl/nt/batch_inverse.hpp | 7 | 2,415 |
| atl/geometry/angular_sort.hpp | 2 | 2,280 |
| atl/nt/series_composition.hpp | 1 | 2,142 |
| atl/ds/detail/heterogeneous_key.hpp | 3 | 2,049 |
| atl/ds/detail/dynamic_forest.hpp | 1 | 1,637 |
| atl/ds/detail/key_value_entry.hpp | 3 | 1,569 |
| atl/nt/online_convolution.hpp | 2 | 1,330 |
| atl/types/algebraic.hpp | 1 | 1,298 |
| atl/config.hpp | 13 | 1,066 |
| atl/ds/detail/persistent_arena.hpp | 1 | 789 |
| atl/ds/detail/splay_layout.hpp | 1 | 452 |
| atl/ds/detail/algebra.hpp | 1 | 443 |
| atl/types/vertex.hpp | 1 | 144 |
<!-- END headers -->

## Reachability: what the examples use and what stays dormant

### What the reports can establish

`cause=` identifies the first retaining predecessor, not every reference. An AST edge can mean a type, a member's owner, a constraint or a system entity; a name edge can come from an uninstantiated function body, a member declaration or a collision. A function called by `main` can receive its first reason from a different, earlier owner. Walking only AST reasons from `main` reaches just 1–4 reported families per example, an obvious undercount of the real executions. **Absence from that forest is not evidence of dead code.** Conversely, a spelling edge is a positive explanation for why text must currently remain, even if its owner is never instantiated.

The [57 retaining witnesses](large-examples/witnesses.csv) include the exact family IDs, original file/line, effective reason, byte ownership and predecessor chain for the main clusters. To strengthen the negative execution evidence, `profile.py` compiled and ran every original and compact example with GCC 15.2.0; all thirteen stdout comparisons passed. Compact builds used `-O0 -fno-inline --coverage`. Coverage is additional evidence about these fixed demonstrations, not a portable whole-program or all-input proof.

### Paths from `main`, per example

These are source/retention paths checked against the locally executed function instances. An arrow is a summarized dependency/call path, not a claim that every report edge is a resolved call. Streaming below always means streaming a modular integer; printing `value.value()` streams a builtin integer instead.

| Example | Used path from `main` | Largest dormant or alternative surfaces |
| --- | --- | --- |
| series-composition-quick | `series_composition` → `composition_dispatch<kinoshita_li>` → the small Horner route; reversion → `composition_inverse_lagrange` → `composition_project`/`composition_graeffe` and `series_power`; public projection → repeated products | Common S/B/W/F pools below; Brent–Kung/block sizing is never instantiated. Kinoshita–Li and Newton reversion are emitted with zero calls in this run. **Graeffe is actually called**, so its 3,778 B cannot be labelled wholly unused just because the precision is five. |
| polynomial-points-quick | point-tree construction → storage/build; `evaluate` → Horner small case; `interpolate` → Newton small case; Taylor shift → factorial scaling/product; root polynomial → sequential leaf products | S/B/W/F; the tree's large descent, middle-product and remainder routes remain available for larger data. Taylor-shift fallback `points_shift_divide` is named when length exceeds the field characteristic, although the example's length is three. |
| power-series-powers-and-roots | normalized power → logarithm/exponential; normalized square root → scalar modular root/coupled root; polynomial division; sparse inverse recurrence | S/B/W/F; Newton transforms and wide-limb product alternatives. Scalar `square_root_mod` is used and must not be counted dead with the optional series extensions. |
| polynomial-gcd-quick | gcd/extended gcd/inverse-mod → Euclidean reduction → ordinary polynomial division/product | S/B/W/F; `gcd_half` (1,866 B) and `gcd_matrix_product` (2,200 B) are unused by these small polynomials. They are named inside the retained reduction machinery and are necessary for the generic large-input route. |
| polynomial-gcd-pade | `pade_approximant` → `pade_dispatch` → the same Euclidean reducer/divider | The same S/B/W/F and 4,066 B half-gcd/matrix-product pool. Neither matrix polynomial here means the unrelated dense `atl::matrix` API. |
| point-location-quick | integer segments → `arrangement_of` → segment sweep/assembly; integer `point_location` → slab frame/persistent status; three `location_of` calls | H/G: float expansion predicates and unrelated hash key specializations. The `uint64` edge map, its iterator `end`, integral widened predicates and `int256` product comparisons are used. Removing all hashing or wide arithmetic would be incorrect. |
| linear-recurrence-quick | Berlekamp–Massey; term query → small schoolbook recurrence; rational coefficient → Bostan–Mori products → convolution's naive tier | S/B/F plus M. No dense matrix or Gaussian elimination instance is executed. `rhs.inverse()` in modular division supplies the spelling `inverse` that follows unrelated matrix inverse overloads. |
| polynomial-points-sampling | geometric evaluation → chirp/product machinery; sample shift → factorial-scaled product; polynomial product → balanced products | S/B/W/F. No point-tree interface is called; it is already removed, so splitting that public interface would not save its whole source header a second time. |
| link-cut-tree-basic | vertex-value constructor → link/expose/splay/push/pull; fold, reroot, parent/depth, cut/connected/try-fold | L is the largest dormant cluster; `ValuePlacement=void`, `has_edge_values=false`. H and unused tagged/subtree/layout/member surfaces remain too. Vertex splay operations are live; the entire 33,053 B link-cut header is not an unused pool. |
| polygon-overlay-quick | integer polygons → `overlay_of` → simplicity/sweep/arrangement; four face-area reductions | H/G. The integer edge map and exact integral geometric predicates are live, despite the arrangement storing rounded double vertex positions and areas. No adaptive floating predicate instance appears in coverage. |
| power-series-quick | inverse recurrence; logarithm → derivative/fused division/integral; exponential recurrence/coupled wrapper | S/B/W/F. Transform-backed bodies are retained for larger requested precision, even though these calls request five terms. Already below the modelled limit. |
| online-convolution-partitions | fixed-kernel constructor; eleven pushes; modular division to form partition numbers | S/B/W/F. Small scheduling paths never execute convolution/NTT/FFT functions. `schedule` itself must not be classified wholly dead: its small branches are part of the used implementation. Already below the modelled limit. |
| online-convolution-quick | online-product constructor; eleven paired pushes to form Catalan numbers | S/B/W/F. The cached transform machinery is idle at these sizes. The uncalled generic `series_product` body nevertheless spells the wide-limb helper. Already below the modelled limit. |

### Largest retention clusters and exact triggers

The following byte figures describe **existing source pools or net prototype edits**, as identified explicitly. They are not all safe distill deletions, and per-example and aggregate sizes must not be mixed.

| Cluster | Compact footprint / affected examples | Exact retaining token or rule; why execution does not require it |
| --- | --- | --- |
| Modular streaming (S) | `write_uint128` **2,607 B**, `read_stream_magnitude` **2,211 B**, its result record **245 B**; uncalled hidden `<<`/`>>` bodies **711 B**. Net **5,774 B × 10 = 57,740 B**; complete helper header is 5,316 B/example including directives/shells. | `mod_int.hpp:152,166` hidden stream operators, charged inside the protected `mod_int` record, spell **`write_uint128`** and **`read_stream_magnitude`**. The output branch tests `std::same_as<T, atl::uint128>`; even its unselected spelling keeps the 128-bit writer. The parser selects its magnitude type using `sizeof(T) > sizeof(uint64)`. No modular stream operator or helper has a runtime instance in these examples. Deleting only a helper breaks name lookup. |
| Primality's sieve/container exposure (B) | Static-vector header **3,550 B**, sieve residue **447 B**, forwarding helpers **909 B**: **4,906 B × 10 = 49,060 B**. | `basic.hpp:6` includes **`sieve.hpp`** to support factorization, although these callers need primality. `static_vector` is protected by hidden **`operator==`/`operator<=>`**; its ordinary members also match A12 tokens such as `resize`, `push_back`, `release`. Its container surface is not used by these ten examples. NTT's own `primitive_root` factors `mod-1` with a local array, so it does not justify retaining the sieve container. |
| Native versus wide series product (W) | `convolution_uint64` **3,218 B**, discarded limb branch **1,003 B**: **4,221 B × 9 = 37,989 B**. | **`convolution_uint64`** occurs in `detail/power_series.hpp:121`, under the `else` of the condition at line 97 testing modulus width and `has_widen_mul_v`. All nine coefficient types are native `uint32` residues modulo 998244353, so that branch is discarded. The generic body must still parse and its name lookup must still succeed. |
| Default hash's unrelated key types (H) | **6,305 B × 3 = 18,915 B** net deletion of non-scalar specializations and their helpers. | All the **`hash`** specializations aggregate; their **`operator()`** members are D3e rule-1 kinds. The floating call operator spells **`hash_float_value`** (`hash.hpp:269`, 654 B), and other operators keep byte/string/pair/tuple/vector/array helpers. Geometry hashes packed `uint64` edge keys; link-cut's selected vertex tier does not use the edge table. This scenario preserves integer/identity hashes and the executed seed initializer. |
| Integral versus floating predicates (G) | Floating helper declarations **5,766 B** per geometry example, plus **264 B** removed from the two generic exact-policy dispatches: **6,030 B × 2 = 12,060 B**. | **`adaptive_orientation`** and **`adaptive_cross_sign`** are nondependent names inside `exact::orientation`/`cross_sign` floating `if constexpr` arms (`predicates.hpp:350,364`). They pull **`adaptive_cross_tail`** (2,073 B), error-free transforms, expansion summation and error bounds. Coordinate types here are integral. `is_finite` is deliberately retained because surviving checked-query template bodies still spell it. |
| Vertex versus edge registry (L) | **18,578 B** net, link-cut only; combined L+H saves **24,883 B** in that example. | **`using edge_registry = forest_edge_registry`** at `link_cut_tree.hpp:577` and the unselected `std::conditional_t<has_edge_values,...>` field expose the full registry. Its A12 methods `acquire`/`release` and spelling paths from `cut` retain insert/find/container machinery. Replacing the registry definition by a **forward declaration**, and avoiding its private container includes, compiles for the selected vertex tier. The hash seed initializer remains present, so the measured gain does not erase that startup effect. |
| Recurrence's matrix/elimination dependency (M) | Matrix header **7,659 B** plus **3,145 B** of elimination declarations, keeping `exact_field`: **10,804 B**, recurrence only. | `linear_recurrence.hpp:4` includes **`gaussian_elimination.hpp`** for **`exact_field`**. A separate collision follows the **`inverse`** token in `mod_int::operator/=` (`mod_int.hpp:98`) into `atl::inverse` and `detail::inverse` (`gaussian_elimination.hpp:271,172`). Matrix hidden operators then protect its record. No matrix is constructed here. |
| Unselected composition strategy (T) | Brent–Kung **1,317 B**, block-size helper **318 B**, dispatcher simplification **428 B**: **2,063 B**, composition only. | **`composition_brent_kung`** is named in `composition_dispatch`'s `Algorithm == ...::brent_kung` arm (`series_composition_extensions.hpp:69`). Both retained dispatcher uses select **`kinoshita_li`**. The helper is uninstantiated; moving only its definition while leaving this nondependent lookup unprovided is insufficient. |
| FFT/nonfriendly fallback (F), a sample-idle pool | **6,512 B × 10 = 65,120 B** net in the explicit direct-NTT policy prototype. | Retained **`convolution`** spells **`fft_split_convolution`**, **`garner_fits`**, **`crt_primes`** and raw CRT transforms after its friendly-prime fast return. The FFT helper is **2,492 B**; `fft.hpp` contributes **2,036 B**. Emitted FFT-split instances have zero calls in all ten runs, since their products take the small naive route. Unlike W, this is not a discarded type arm: unsupported transform sizes can reach it in the generic API. |

The false branch in W and the unselected T algorithm are stronger evidence than “the sample used small arrays.” The G type separation is also strong after tracing all predicate receiver types; rounded storage alone must not be used to infer those types. F and half-gcd are deliberately classified as generic alternatives, not globally unreachable code.

### Function-source execution accounting, per example

This table partitions only the owned bytes of separately reported functions/methods. “Executed” means at least one local instance had a positive counter; it does **not** mean every branch in that source body executed. “No runtime instance” includes uninstantiated templates, compile-time-only computations, unused inline functions and possible instrumentation omissions. Signatures still have parsing, lookup, constraints and overload obligations. Hidden friends charged to a record, record layout, directives and other non-function ownership are outside this table. **Neither idle column is a portable removable-byte estimate.**

<!-- BEGIN coverage -->
| Example | Executed function-source B | Emitted, zero executions B | No runtime instance B |
| --- | --- | --- | --- |
| series-composition-quick | 42,292 | 24,910 | 21,665 |
| polynomial-points-quick | 19,178 | 31,374 | 21,046 |
| power-series-powers-and-roots | 31,166 | 18,182 | 19,231 |
| polynomial-gcd-quick | 21,227 | 24,257 | 21,133 |
| polynomial-gcd-pade | 20,004 | 23,554 | 21,133 |
| point-location-quick | 34,589 | 653 | 24,375 |
| linear-recurrence-quick | 12,855 | 16,092 | 24,807 |
| polynomial-points-sampling | 21,678 | 15,555 | 20,512 |
| link-cut-tree-basic | 16,493 | 0 | 31,525 |
| polygon-overlay-quick | 27,485 | 563 | 24,349 |
| power-series-quick | 16,977 | 16,896 | 20,902 |
| online-convolution-partitions | 9,419 | 20,375 | 23,193 |
| online-convolution-quick | 6,637 | 20,375 | 25,550 |
<!-- END coverage -->

## ATL-side changes and size estimates

Each scenario is independently measured against the original compact artifact. `candidates.py` applies the corresponding edits to a **worktree-local prototype**, compiles it and compares stdout with the original example. None edits ATL or production distill. A real library refactor should retain the existing broad umbrella headers and add narrow dependency layers; optional APIs must remain available to clients that explicitly include their extensions.

The measured savings below include the edits needed to remove the naming owner/path. They are more informative than subtracting a helper's size while leaving an ill-formed caller. They still exclude new production wrapper/constraint/header scaffolding, unless present in the prototype. The final combined count remains **12/13 even if 512 compact bytes are added back for every applicable scenario in every example**; the closest passing projections then are points-quick 63,810 B and point-location 63,512 B. That buffer is a sensitivity check, not a proof of the eventual implementation size.

### Recommended implementation shapes

1. **S — move modular I/O to an opt-in header.** Put the two stream operators and uint128 parsing/formatting helpers in `mod_int_io.hpp`; use the arithmetic core inside polynomial/series headers and retain an umbrella for existing users. The examples print `.value()` already. Savings: **5,774 B in each of ten**. This alone additionally fits recurrence and sampling. Removing just the uint128 writer's name is weaker: the input parser is needed even by the unused 32-bit extraction template, and hidden-friend/operator protection keeps its owner.
2. **B — split primality from factorization/sieve.** Move `is_prime`/Miller–Rabin into a small dependency layer; make NTT/prime-modulus checks include that rather than the broad `basic.hpp`/sieve route. Keep factorization and sieve interfaces in their existing umbrella. Savings: **4,906 B in each of ten**. It fits the same two examples as S; these fit counts cannot be added. Do not remove `forward_element.hpp` from the geometry/tree programs: their range constructors need it. The prototype explicitly limits B to the ten static-vector-bearing examples.
3. **W — isolate the full-width product specialization.** Put the limb product and `convolution_uint64` behind an implementation specialization/extension header. The core overload can be constrained to native 32-bit coefficients and retain the same product result; the full generic interface must dispatch to the wide implementation when its header is used. Savings pool: **4,221 B in nine**. Changing only `if constexpr` syntax while leaving every helper spelling in the same retained generic body does not solve distill's name closure.
4. **M — extract `exact_field` as a concept-only header.** Recurrence needs the field contract, not Gaussian elimination and dense matrix arithmetic. Savings: **10,804 B**, comfortably fitting recurrence. Descriptive matrix API names such as `matrix_inverse` would also avoid the `inverse` collision, but renaming just that helper is not credited with all 10.8 KB: the included matrix operators independently protect the record.
5. **H — separate scalar hashing from extended key specializations.** Keep the shared seed, integer hash and identity hash in the core; place floating, strings, pointers and composite-key specializations in extensions. Integer-only private maps in geometry can include the core. Existing general flat-hash umbrellas must continue providing their documented key support. Savings: **6,305 B in three**, newly fitting link-cut and polygon-overlay. It leaves point-location at **67,885 B**, so H alone is insufficient there.
6. **G — split integral predicates from adaptive floating implementations.** Provide an integral policy/dependency layer, with floating-policy definitions in the extension. Retain the existing exact generic umbrella for floating clients. Use type-dependent implementation selection or a narrow integral policy so an uninstantiated floating arm does not still require the large helpers. Savings: **6,030 B in two**, newly fitting polygon-overlay; H+G fits point-location as well. Removing exactness or substituting epsilon comparisons is not proposed.
7. **L — expose the edge registry lazily.** Forward-declare the private registry in the vertex-capable core; place its definition and hash-map includes in the edge-value extension. The false `conditional_t` arm may name an incomplete type. Edge users still need the full definition before instantiation. The local prototype actually retains the original `edge_registry` alias; it is not a substituted dummy implementation. Savings: **18,578 B**, link-cut only; startup seed initialization is preserved. This is preferable to changing the vertex algorithm to save eight forecast bytes.
8. **T — separate composition strategies.** Put Brent–Kung and its block sizing in its strategy header; replace the broad dispatcher body with strategy-specific definitions. Keep Horner small cases and the default Kinoshita–Li implementation. Savings: **2,063 B**, no newly fitting example alone. Lagrange versus Newton reversion and characteristic-dependent Taylor-shift/power paths offer further specialization work, but no unmeasured cascade is added to this estimate.
9. **F — offer a direct-NTT product policy/core.** Keep exact arithmetic for admitted friendly-prime sizes, naive multiplication for small products, and the broad FFT/CRT dispatcher as an optional policy. The measurement uses schoolbook as the nonfriendly fallback, so its values still match the examples. **It changes the complexity of unsupported-size products**; it is not a drop-in refactor of the generic convolution complexity contract. Use an explicit policy or a proved size/domain contract, with the existing generic API intact for other users. Savings: **6,512 B in ten**, additionally fitting recurrence and sampling. Do not infer “998244353” alone rules out fallback: lengths matter, including enlarged composition work buffers.

### Independent scenarios and combinations

“Under” includes the three original examples already fitting; “newly under” is the incremental set relative to that baseline. The full [scenario-by-example table](large-examples/scenarios.csv) records remaining compact bytes and forecast bytes for all thirteen in every scenario. Letter combinations apply the union of source edits; they are not sums of independently rounded forecast sizes. L is an alternative extra reduction for link-cut and is not needed in the main combined bundle.

<!-- BEGIN scenarios -->
| Scenario | Saving per affected example B | Aggregate saving B | Under limit / 13 | Newly under |
| --- | --- | --- | --- | --- |
| S | 5774 | 57,740 | 5 | linear-recurrence-quick, polynomial-points-sampling |
| B | 4906 | 49,060 | 5 | linear-recurrence-quick, polynomial-points-sampling |
| W | 4221 | 37,989 | 4 | polynomial-points-sampling |
| M | 10804 | 10,804 | 4 | linear-recurrence-quick |
| H | 6305 | 18,915 | 5 | link-cut-tree-basic, polygon-overlay-quick |
| G | 6030 | 12,060 | 4 | polygon-overlay-quick |
| T | 2063 | 2,063 | 3 |  |
| F | 6512 | 65,120 | 5 | linear-recurrence-quick, polynomial-points-sampling |
| L | 18578 | 18,578 | 4 | link-cut-tree-basic |
| SBW | 10680, 14901 | 144,789 | 8 | power-series-powers-and-roots, polynomial-gcd-quick, polynomial-gcd-pade, linear-recurrence-quick, polynomial-points-sampling |
| HG | 6305, 12335 | 30,975 | 6 | point-location-quick, link-cut-tree-basic, polygon-overlay-quick |
| LH | 6305, 24883 | 37,493 | 5 | link-cut-tree-basic, polygon-overlay-quick |
| SBWMHGT | 6305, 12335, 14901, 16964, 21484 | 188,631 | 11 | power-series-powers-and-roots, polynomial-gcd-quick, polynomial-gcd-pade, point-location-quick, linear-recurrence-quick, polynomial-points-sampling, link-cut-tree-basic, polygon-overlay-quick |
| SBWMHGTF | 6305, 12335, 21413, 23476, 27996 | 253,751 | 12 | polynomial-points-quick, power-series-powers-and-roots, polynomial-gcd-quick, polynomial-gcd-pade, point-location-quick, linear-recurrence-quick, polynomial-points-sampling, link-cut-tree-basic, polygon-overlay-quick |
<!-- END scenarios -->

The broad practical bundle is **S+B+W+M+H+G+T+F**, subject to F's explicit policy/domain restriction. It removes **253,751 compact bytes** across the population. Its per-example result is:

<!-- BEGIN bundle -->
| Example | After combined splits B | Projected B |
| --- | --- | --- |
| series-composition-quick | 87,194 | 76,173 |
| polynomial-points-quick | 70,994 | 62,021 |
| power-series-powers-and-roots | 67,525 | 58,990 |
| polynomial-gcd-quick | 66,326 | 57,943 |
| polynomial-gcd-pade | 64,563 | 56,403 |
| point-location-quick | 71,677 | 62,618 |
| linear-recurrence-quick | 47,800 | 41,759 |
| polynomial-points-sampling | 56,575 | 49,424 |
| link-cut-tree-basic | 68,720 | 60,034 |
| polygon-overlay-quick | 62,725 | 54,797 |
| power-series-quick | 52,541 | 45,900 |
| online-convolution-partitions | 52,263 | 45,657 |
| online-convolution-quick | 51,832 | 45,281 |
<!-- END bundle -->

### The remaining series-composition example

The combined prototype still needs another **12,177 compact bytes** removed to satisfy the model. It would be misleading to promise that moving the 1,319 B Newton reversion or the already-counted unused strategy alone closes that gap. Graeffe/projection work and the generic series-power machinery are real dependencies of the current fast API, even in the precision-five example.

An optional **E — elementary series header** is a concrete alternative if the maintainer wants this particular quick example to demonstrate composition/reversion/projection without the fast implementation. [The analysis script](large-examples/elementary.py) uses Horner with truncated schoolbook multiplication, the library's triangular reversion recurrence, and repeated-power projection. It retains the compact artifact's **entire 29,334 B modular-integer dependency surface**, including streaming, rather than assuming further pruning. Readable kernels add **2,207 B**. With the example and includes, the actual prototype is **32,461 B**, saving **78,209 B** from this example alone; the forecast is **28,358 B**. No one-line output, one-letter renaming, output hardcoding or disabled exact arithmetic is involved.

The original demonstration's stdout matches. A separate executable compares all three elementary operations with ATL's original fast functions on **260 deterministic generated cases**, lengths 0–12, including nonzero inner constants for finite composition, and verifies composition with the inverse gives the identity series. This supports the small API option; it is not a benchmark or a proof for unlimited precision. Reversion is cubic. Document a small-precision intended domain and retain the fast API for large inputs. **E alone gives 4/13 fitting; replacing only the remaining composition case by E alongside the main bundle gives 13/13.** If the name and fast-algorithm focus of the existing example must remain, report 12/13 and continue algorithm/dependency work instead.

## Distill-side candidates, assumptions, and risks

The prior reviews distinguish a positive spelling/lookup proof from worker-only specialization. That distinction matters more than the numerical size of an unused function. The following are candidates for these thirteen examples, not production changes or adopted assumptions. Immediate-reason pools are ceilings; other protections and alternate retaining edges can remain.

| Candidate | Concrete benefit here | Assumption/proof needed; risk |
| --- | --- | --- |
| **Definition demand inside retained classes/records** | S exposes **5,063 B of helper declarations plus 711 B of hidden stream bodies per algebra example**. B exposes the unused static-vector record/operator surface. | Need to remove or defer the naming bodies as well as their helpers. Defaulted/hidden operators affect overload sets, ADL, constraints, rewritten comparisons and library probes. Under current A6/A7/D3e rules, “GCC did not emit this member” is insufficient. A new explicit **all-target non-instantiation/non-observation certificate for the entire owner group** could authorize this; accepting the observed worker instances as exhaustive would be a stronger, risky assumption. |
| **Closed-domain template/body specialization** | W's **37,989 B** measured pool; G's **12,060 B** scenario; T's **2,063 B**; more characteristic-dependent alternatives may follow. | Require that all reachable substitutions, including judge-library probes and instantiation effects, are in a certified type/constant set. Preserve signatures, constraints, failed lookup/candidate effects, constant-evaluation semantics, roots and pins. Merely deleting the `if constexpr` branch discarded in the worker changes a retained generic definition for other possible substitutions. A targeted integral/native-32 API is easier to certify than a global “only observed instantiations” assumption. |
| **Target-stable member/qualified-name bindings, extending D3f** | Recurrence's **`inverse`** witness is concrete: `rhs` is `mod_int`, while `atl::inverse(matrix...)` is unrelated. The broad name group owns **747,735 B**, but that is not the saving. M's 10,804 B is an ATL include-split result, not all attributable to this one collision. | Prove the receiver and binding exclude the foreign candidate on every allowed target, not only the worker. Account for aliases, dependent bases, macro expansions, using/friend declarations, failed probes and effects. Suppressing just a function-declarator token will not fix a genuine `.inverse()` spelling. Recompute the full group/name closure after suppression and preserve the static-name invariant. |
| **A12 receiver/hook certificates** | A12 owns **85,114 B** here. Link-cut keeps `assign`, `apply`, `for_each` and unrelated API surfaces; hash/map overloads offer similar cases. | A12 currently bounds member **spelling**, not receivers. Narrowing requires an audited all-target library hook and type-flow certificate, including dormant implementation paths and macros. Names such as `begin`, `end`, `size`, `reserve` can be actual protocols. The two 61 B dependent `end` declarations are used in geometry and should not be treated as a free saving. **0 B is certified removable by a new A12 rule in this analysis.** |
| **Owner/dependency groups with incomplete optional types** | L demonstrates **18,578 B** can disappear for a vertex-only interface while retaining the registry's name as a forward declaration. | A general extractor rule needs proof that no surviving declaration requires completeness, layout, constructors, constraints or effects of the optional type on any judge. Preserve reverse possible-effect edges and library exposure. The prototype demonstrates this one selected instantiation, not a general incomplete-type pruning rule. ATL controlling the include boundary is the lower-risk route. |
| **Bounded-value/algorithm policy pruning** | F's **65,120 B** pool and the two **4,066 B** half-gcd/matrix-product pools are idle in these sample executions. | Need sound value-range/domain proofs or an explicit restricted API; sample coverage is not enough. `convolution` can reach FFT/CRT for other sizes even with the same modulus. Do not delete constant-evaluation arms merely because a runtime sample never uses them. Replacing algorithms also needs the relevant complexity contract preserved or explicitly changed. |
| **Repeated includes and consumed generated guards** | Exact compact text contains **31,878 B of all includes** and **14,820 B of generated guards**. The requested 4% estimate is **42,979 B** on this population before shortening; this is already modelled, not an extra saving. | Preserve first include positions; prove approved-header idempotence under unchanged relevant macro state. Generated guard erasure needs a no-observation certificate for invented macro names, including library tests. These are the same gaps as the previous review. All includes is an upper bound, not the repeated-include pool. No second 4% or wholesale include subtraction is added. |

Do not relax instantiation-effect protection just because these demonstrations have simple input types. `hash_seed_value` performs actual startup work here; optional containers cannot silently authorize deleting it. Likewise, compile-time primality/constraints and unused signatures can still determine well-formedness. The safe implementation direction is positive receiver/type/completeness evidence with fail-closed handling, or explicit narrow ATL interfaces. No obfuscation or single-line formatting is proposed.

## Verification and reproduction

Only this review, its analysis scripts and exported tables were added. Inputs were not modified. All compiler products, coverage, logs, installed wheels and JSON intermediates are under ignored **`measurements/large-examples/`** inside the worktree. There are no C++ production edits or changes to the previous review scripts.

From the worktree root:

```sh
mkdir -p measurements/large-examples/tmp measurements/large-examples/python
TMPDIR="$PWD/measurements/large-examples/tmp" python3 -B -m pip --isolated install \
  --no-cache-dir --disable-pip-version-check --target measurements/large-examples/python \
  tree-sitter==0.27.0 tree-sitter-cpp==0.23.4
python3 -B docs/reviews/large-examples/analyze.py
python3 -B docs/reviews/large-examples/profile.py
python3 -B docs/reviews/large-examples/candidates.py --verify
python3 -B docs/reviews/large-examples/elementary.py
python3 -B docs/reviews/large-examples/render.py
python3 -B docs/reviews/large-examples/check.py
git diff --check
```

The scripts pin the supplied external read-only paths in `common.py`; they do not search for credentials, invoke production distill, edit ATL, or contact a judge. GCC 15.2.0 and matching gcov were available locally. `--only` on `candidates.py` supports verifying changed scenarios without rebuilding already checked ones. Full prototype checks compile the union of edits, including the combined bundles, rather than assuming independent successes compose.

Checks passed: **13 exact byte ledgers**, **13 complete output-line provenance maps**, **3,566 mapped written family occurrences**, **13 original/compact compile-and-stdout comparisons**, **89 compiled and stdout-matching changed scenario/example prototypes**, **260 elementary equivalence/property cases**, and complete exported-matrix reconciliation. All **131 recorded input hashes** remained unchanged; the [manifest](large-examples/inputs.sha256) identifies the four input artifacts per example and original source files used by the measurements. These local runs supplement the supplied worker evidence; neither proves another judge's behavior or validates an unimplemented generic ATL refactor.
