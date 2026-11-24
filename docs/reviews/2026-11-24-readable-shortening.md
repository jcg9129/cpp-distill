# Readable identifier-shortening analysis (2026-11-24)

Analysis only; no production changes. Base `origin/main` / `2256cfe`, branch
`abbrev-analysis`. The same 126 ATL compact outputs as the
[earlier spelling analysis](2026-11-24-minify-analysis.md) are measured:
distill main with D3f, no indentation, existing statement line breaks retained.

Readable shortening **R saves 349,578 B (9.17%)**, versus 873,810 B (22.93%) for
the earlier shortest-name T2. R leaves **15** examples
over 65,536 B; R plus space collapse and the estimated PR #129 cleanup saves
**510,034 B (13.38%)**, leaving **10** over that limit.
Readability is illustrated by actual renames below; this does not certify
acceptability under a judge's submission rules.

## Totals and source-size limits

R denotes readable renaming in this report. The earlier report used R for
include/define removal; that operation is called **PR129** here. “Spaces” means
collapse runs of ASCII spaces between tokens on one physical line to one space.
It keeps literal, comment and header-name contents, tabs and every line break.
“Over” is strictly greater than the cap; equality fits. Shared header text is
counted at every occurrence in every output.

| Measure | Compact | R | R + spaces | Compact + PR129 | R + PR129 | R + spaces + PR129 |
|---|---:|---:|---:|---:|---:|---:|
| Total bytes | 3,811,229 | 3,461,651 | 3,456,885 | 3,655,539 | 3,305,961 | 3,301,195 |
| Bytes saved from compact | 0 | 349,578 | 354,344 | 155,690 | 505,268 | 510,034 |
| Reduction | 0.00% | 9.17% | 9.30% | 4.09% | 13.26% | 13.38% |
| Examples over 65,536 B | 17 | 15 | 15 | 17 | 10 | 10 |
| Examples over 50,000 B | 23 | 22 | 22 | 23 | 22 | 22 |
| Examples over 32,768 B | 41 | 35 | 35 | 36 | 33 | 33 |

R changes **13,738 per-output spellings**. Collision
resolution is exercised for 292 eligible spellings
(including ones restored to their original spelling). The additional saving from
collapsing spaces after R is **4,766 B**; existing
punctuation spacing is otherwise retained.

## Fifteen most frequent actual renames

Counts are identifier-token occurrences, summed over the 126 outputs for each
**(old, new) pair**, sorted by decreasing frequency and then alphabetically.
A spelling can have different expansions in different outputs because the
collision sets differ. These are measured renames, including common header text,
not hand-picked dictionary demonstrations.

| Original | R | Occurrences |
|---|---|---:|
| `uint32` | `unt32` | 7,017 |
| `detail` | `dtl` | 5,481 |
| `uint64` | `unt64` | 3,637 |
| `float64` | `flt64` | 2,212 |
| `point` | `pnt` | 1,734 |
| `Coefficient` | `Coef` | 1,579 |
| `pool_` | `pl_` | 1,163 |
| `uint128` | `unt128` | 1,115 |
| `Value` | `Val` | 1,085 |
| `traits` | `trts` | 905 |
| `vertex_id` | `vtx_id` | 840 |
| `size_` | `sz_` | 808 |
| `table_` | `tbl_` | 750 |
| `Engine` | `Engn` | 734 |
| `Graph` | `Grph` | 674 |

## Still over 64 KiB

The best measured variant, R + spaces + PR129, still leaves **10**
examples over 65,536 B.

All examples exceeding 65,536 B after R are listed below, so the effect of each
additional operation is visible. Remaining over-limit best-variant sizes are bold.

| Example | R | R + spaces | R + PR129 | R + spaces + PR129 |
|---|---:|---:|---:|---:|
| `linear-recurrence-quick` | 70,687 | 70,648 | 68,235 | **68,196** |
| `link-cut-tree-basic` | 67,124 | 67,085 | 65,171 | 65,132 |
| `online-convolution-partitions` | 68,373 | 68,334 | 64,927 | 64,888 |
| `online-convolution-quick` | 68,028 | 67,989 | 64,582 | 64,543 |
| `point-location-quick` | 75,934 | 75,894 | 73,367 | **73,327** |
| `polygon-overlay-quick` | 67,625 | 67,585 | 65,451 | 65,411 |
| `polynomial-gcd-pade` | 77,474 | 77,435 | 73,955 | **73,916** |
| `polynomial-gcd-quick` | 79,033 | 78,994 | 75,533 | **75,494** |
| `polynomial-points-quick` | 83,130 | 83,091 | 79,798 | **79,759** |
| `polynomial-points-sampling` | 71,525 | 71,486 | 68,193 | **68,154** |
| `power-series-powers-and-roots` | 79,890 | 79,851 | 76,944 | **76,905** |
| `power-series-quick` | 68,220 | 68,181 | 65,274 | 65,235 |
| `series-composition-quick` | 97,330 | 97,291 | 93,570 | **93,531** |
| `subtree-link-cut-tree-basic` | 71,487 | 71,448 | 69,397 | **69,358** |
| `top-tree-basic` | 98,147 | 98,108 | 96,156 | **96,117** |

## Exact scheme R

The implementation is [abbreviate.py](readable-shortening/abbreviate.py).
Identifiers of at most four characters remain unchanged. Eligible longer
identifiers are split at underscores, letter/digit transitions and lowerCamel /
UpperCamel boundaries, including acronym-to-title boundaries (`XMLStream` becomes
`XML` + `Stream`). Underscore runs, leading/trailing underscores and digit runs
are copied in place. Words of at most three letters remain unchanged. Longer
words first use the fixed 64-entry dictionary below; otherwise keep the first
letter, remove later `a/e/i/o/u` vowels case-insensitively, then cap at four letters.
`y` is retained. Lowercase, title case and uppercase words retain their case style.
The original separators and camel style join the abbreviated words.

Examples before collision resolution: `read_stream_magnitude` becomes
`rd_strm_mgnt`; `isUnsignedIntegral` becomes `isUnsgIntg`; `magnitude` becomes
`mgnt`, `stream` becomes `strm`, and `unsigned` becomes `unsg`.

Allocation priority is descending occurrence count within one output, with
alphabetical ties. All original identifier spellings (even those scheduled for
renaming), all exclusions, library names, keywords, predefined macro names and
already allocated results are reserved. The original spelling of the identifier
being allocated is available to itself. On collision, expand shortened words
left-to-right in rounds: increase one word's length by one and restore that many
letters from its original prefix. Test uniqueness after each step. This handles
dictionary abbreviations that are not subsequences, such as `vertices -> vtxs`.
After restoring all words, decimal suffixes starting at `1` are the final fallback.
In this corpus restoration always makes the original name available before that
fallback is needed. Final names are never keywords or reserved identifiers;
standalone `integer -> int` is therefore rejected and lengthened, while `int`
inside a compound identifier is permitted. The complete substitution is injective
over all source identifier spellings in each output, and applied simultaneously.

### Fixed dictionary (64 entries)

| Word | Abbreviation | Word | Abbreviation |
|---|---|---|---|
| `answer` | `ans` | `argument` | `arg` |
| `buffer` | `buf` | `capacity` | `cap` |
| `child` | `ch` | `children` | `chs` |
| `coefficient` | `coef` | `comparator` | `cmp` |
| `compare` | `cmp` | `count` | `cnt` |
| `current` | `cur` | `degree` | `deg` |
| `denominator` | `den` | `depth` | `dep` |
| `destination` | `dst` | `difference` | `diff` |
| `distance` | `dist` | `element` | `elem` |
| `function` | `func` | `height` | `ht` |
| `identity` | `ident` | `index` | `idx` |
| `initialize` | `init` | `input` | `in` |
| `integer` | `int` | `inverse` | `inv` |
| `iterator` | `it` | `left` | `lft` |
| `length` | `len` | `matrix` | `mat` |
| `maximum` | `max` | `middle` | `mid` |
| `minimum` | `min` | `modulus` | `mod` |
| `multiplicity` | `mult` | `number` | `num` |
| `numerator` | `num` | `operation` | `op` |
| `operator` | `oper` | `output` | `out` |
| `parameter` | `param` | `parent` | `par` |
| `pointer` | `ptr` | `polynomial` | `poly` |
| `position` | `pos` | `prefix` | `pref` |
| `previous` | `prev` | `query` | `qry` |
| `read` | `rd` | `result` | `res` |
| `right` | `rgt` | `segment` | `seg` |
| `size` | `sz` | `source` | `src` |
| `string` | `str` | `suffix` | `suff` |
| `temporary` | `tmp` | `update` | `upd` |
| `value` | `val` | `vector` | `vec` |
| `vertex` | `vtx` | `vertices` | `vtxs` |
| `weight` | `wt` | `write` | `wr` |

## Exclusions and provenance

The earlier [measure.py](minify-analysis/measure.py) and
[clang_inventory.py](minify-analysis/clang_inventory.py) are imported unchanged.
Libclang identifies user-source declarations; renaming is global by spelling
within one output, covering declarations, references and ordinary macro bodies.
This preserves the earlier exclusion set exactly before adding language protocols
and predefined macro blocking: libstdc++-13/header spellings, every A6 ADL-registry
name, C++ keywords and conservative contextual keywords, defined macros and macro
parameters, all identifiers in replacement lists containing `#`/`##` (including
digraphs), string-literal contents (also decoded octal/hex escapes), literal
suffixes, attribute names/payloads, reserved names (`__` anywhere or leading `_`
plus uppercase), and `main`. Literal and comment bytes are never renamed.

The additional fixed language-protocol list is:
`await_ready`, `await_resume`, `await_suspend`, `await_transform`, `begin`, `coroutine_handle`, `coroutine_traits`, `end`, `final_suspend`, `get`, `get_return_object`, `get_return_object_on_allocation_failure`, `initial_suspend`, `promise_type`, `return_value`, `return_void`, `tuple_element`, `tuple_size`, `type`, `unhandled_exception`, `value`, `yield_value`.
It covers coroutine promise/await hooks, tuple-based structured bindings and
range-for lookup. Some are already protected by headers, keywords or ADL;
the explicit list is independent of header reachability. Names from the
compiler's `-std=c++23 -dM` predefined-macro inventory are additionally blocked;
see [predefined-macros.txt](readable-shortening/predefined-macros.txt).

All 126 inputs have **identical SHA-256 hashes and sizes** to the earlier
analysis; see [input-manifest.tsv](readable-shortening/input-manifest.tsv).
The scripts read only the authorized `distill-compact.cpp` external artifacts;
earlier checked-in manifests/results supply the comparison, without reading
external `.rep` or `summary.tsv` files. For every output, the earlier library-name
count, header count, T2 assignment count and T2 saved bytes are reproduced exactly
before applying the additional R rules. All **363** transitive
header hashes also match the earlier [header manifest](minify-analysis/header-manifest.tsv).
This establishes the same exclusion inventory rather than substituting current
system libstdc++ headers.

The pinned package is libstdc++-13-dev **13.3.0-16, arm64**, SHA-256
`5db1835e949dc2682ee27f3732af709bc6a80f9a825a70a389dd84f24ed48078`.
It is unpacked locally using the earlier preparation script. Full raw-header
identifier scanning includes inactive branches and macro definitions, plus
reached host libc and Clang builtin headers. This remains the earlier native
AArch64 approximation, rather than the judge's x86-64 or multi-build A12 inventory.

## PR #129 byte accounting

The earlier `cleanup()` is reused unchanged: keep the first identical standard
angle-bracket include in textual order and count every later entire physical
line; remove an empty `DISTILL_ONCE_*` definition only when its name has one
identifier-token occurrence in the whole original output. The counted original
lines total **87,024 B** for repeated includes and
**68,666 B** for unused once-defines, exactly matching the
earlier per-example columns. Their sum is **155,690 B**.

The PR129 variants actually remove those lines, keep the original R map, and then
optionally collapse spaces; allocation is not rerun. R-only cleanup subtracts
the original-line sum exactly. The space-collapsed variants measure the removed
lines after that operation, avoiding double-counting any already-collapsed spaces.
This is a byte estimate of those two rules, not a general proof that repeated
inclusion is harmless under intervening macro changes.

## Verification and limits

**630/630 GCC syntax checks pass** (five generated variants for all 126 examples),
using GCC 15.2, pinned GCC 13 headers and `-std=c++23 -fsyntax-only`.
**630/630 independent Clang raw-lexer comparisons pass**, against the expected
simultaneous substitution or exact cleanup, with phase-2 splice normalization
outside literals as in the earlier checker. The measurement additionally checks
exact literal bytes, preprocessing-token/directive-group equality, physical line
counts, injectivity and occurrence-based byte savings. **23/23 focused tests pass**
(11 new plus the earlier 12), covering splitting/case/digits, collision allocation,
keywords, all exclusions, protocols, macro adjacency, line accounting and literal
preservation. Verification hashes identify the exact generated files checked.

These are syntax/token checks, not runtime equivalence or judge certification.
The earlier analysis' [soundness limits](2026-11-24-minify-analysis.md#soundness-limits)
still apply: macro invocation stringizing/pasting can observe changed arguments;
target-specific or macro-generated library names can escape raw-header scanning;
generated function/type names and external ABI can expose spelling changes.
Keeping protocol names and line breaks addresses specific risks, without proving
a general C++ renamer sound. Dictionary/consonant shortening is also a subjective
readability tradeoff, visible in the actual samples rather than a compliance claim.

Reproduce using the [script README](readable-shortening/README.md).
Machine-readable data: [results.tsv](readable-shortening/results.tsv),
[results.json](readable-shortening/results.json),
[syntax checks](readable-shortening/verification.json), and
[token checks](readable-shortening/token-verification.json).
Full rename maps, transformed sources and compiler logs remain under ignored
`measurements/readable-shortening/`. No production source is changed.

## Per-example bytes

The last two columns count bytes of original physical lines, not independent
savings to add after space collapse. All requested variant sizes are measured.

| Example | Compact | R | R + spaces | Compact + PR129 | R + PR129 | R + spaces + PR129 | Repeated includes | Unused once-defines |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `aggregate-queue-deque-sliding-window` | 9,963 | 8,713 | 8,674 | 9,532 | 8,282 | 8,243 | 241 | 190 |
| `aho-corasick-quick` | 19,355 | 17,618 | 17,579 | 17,861 | 16,124 | 16,085 | 924 | 570 |
| `arrangement-quick` | 68,155 | 61,733 | 61,693 | 66,113 | 59,691 | 59,651 | 1,244 | 798 |
| `assignment-quick` | 10,789 | 10,175 | 10,136 | 10,406 | 9,792 | 9,753 | 155 | 228 |
| `bipartite-matching-dag` | 34,885 | 31,843 | 31,804 | 32,553 | 29,511 | 29,472 | 1,534 | 798 |
| `bipartite-matching-quick` | 33,115 | 30,181 | 30,142 | 30,783 | 27,849 | 27,810 | 1,534 | 798 |
| `bit-utilities-combinations` | 2,369 | 2,261 | 2,261 | 2,019 | 1,911 | 1,911 | 46 | 304 |
| `bitwise-convolution-quick` | 37,121 | 34,757 | 34,718 | 36,068 | 33,704 | 33,665 | 407 | 646 |
| `calipers-quick` | 46,506 | 40,832 | 40,793 | 45,304 | 39,630 | 39,591 | 670 | 532 |
| `centroid-decomposition-quick` | 33,036 | 29,981 | 29,942 | 30,600 | 27,545 | 27,506 | 1,676 | 760 |
| `circle-quick` | 49,780 | 43,855 | 43,816 | 48,099 | 42,174 | 42,135 | 997 | 684 |
| `closest-pair-quick` | 12,260 | 11,243 | 11,204 | 11,658 | 10,641 | 10,602 | 222 | 380 |
| `convex-hull-quick` | 29,686 | 26,397 | 26,358 | 28,618 | 25,329 | 25,290 | 536 | 532 |
| `convex-piecewise-linear-forest-quick` | 24,606 | 21,493 | 21,454 | 23,623 | 20,510 | 20,471 | 527 | 456 |
| `convex-piecewise-linear-moving` | 15,413 | 13,645 | 13,606 | 14,880 | 13,112 | 13,073 | 191 | 342 |
| `convex-piecewise-linear-quick` | 12,995 | 11,590 | 11,551 | 12,462 | 11,057 | 11,018 | 191 | 342 |
| `convex-polygon-operations-quick` | 74,297 | 65,171 | 65,132 | 72,622 | 63,496 | 63,457 | 991 | 684 |
| `convolution-quick` | 60,073 | 56,665 | 56,626 | 58,160 | 54,752 | 54,713 | 1,039 | 874 |
| `coordinate-compression-basic` | 17,535 | 16,120 | 16,081 | 16,830 | 15,415 | 15,376 | 287 | 418 |
| `coordinate-compression-move-only` | 17,794 | 16,349 | 16,310 | 17,048 | 15,603 | 15,564 | 328 | 418 |
| `counting-sort-stable-records` | 4,120 | 3,855 | 3,816 | 3,968 | 3,703 | 3,664 | 0 | 152 |
| `crt-quick` | 13,043 | 12,005 | 11,966 | 12,460 | 11,422 | 11,383 | 127 | 456 |
| `delaunay-quick` | 62,229 | 55,616 | 55,577 | 59,481 | 52,868 | 52,829 | 1,836 | 912 |
| `difference-fenwick-basic` | 14,441 | 13,288 | 13,249 | 13,638 | 12,485 | 12,446 | 385 | 418 |
| `discrete-log-quick` | 61,410 | 57,006 | 56,967 | 59,255 | 54,851 | 54,812 | 1,243 | 912 |
| `dp-optimization-interval-merge` | 9,528 | 8,835 | 8,796 | 8,758 | 8,065 | 8,026 | 352 | 418 |
| `dp-optimization-partition` | 14,672 | 13,080 | 13,041 | 13,860 | 12,268 | 12,229 | 394 | 418 |
| `dp-optimization-quick` | 7,784 | 7,221 | 7,182 | 7,013 | 6,450 | 6,411 | 353 | 418 |
| `dp-optimization-row-minima` | 7,724 | 7,114 | 7,075 | 7,110 | 6,500 | 6,461 | 234 | 380 |
| `dsu-history` | 19,137 | 17,494 | 17,455 | 18,035 | 16,392 | 16,353 | 570 | 532 |
| `dual-segment-tree-quick-start` | 9,164 | 8,150 | 8,111 | 8,774 | 7,760 | 7,721 | 200 | 190 |
| `dynamic-lazy-segment-tree-quick-start` | 9,363 | 8,566 | 8,527 | 9,112 | 8,315 | 8,276 | 61 | 190 |
| `dynamic-segment-tree-quick-start` | 5,715 | 5,225 | 5,186 | 5,464 | 4,974 | 4,935 | 61 | 190 |
| `euler-tour-tree-basic` | 56,402 | 51,943 | 51,904 | 54,535 | 50,076 | 50,037 | 1,107 | 760 |
| `exact-count-optimum-quick` | 19,060 | 17,058 | 17,019 | 17,912 | 15,910 | 15,871 | 616 | 532 |
| `factorization-quick` | 21,361 | 20,097 | 20,058 | 20,337 | 19,073 | 19,034 | 378 | 646 |
| `fast-io-roundtrip` | 39,558 | 37,047 | 37,008 | 38,219 | 35,708 | 35,669 | 617 | 722 |
| `fenwick-tree-basic` | 13,612 | 12,464 | 12,425 | 13,004 | 11,856 | 11,817 | 228 | 380 |
| `flat-hash-basic` | 33,436 | 31,320 | 31,281 | 32,205 | 30,089 | 30,050 | 699 | 532 |
| `floor-sum-quick` | 8,773 | 7,764 | 7,721 | 8,299 | 7,290 | 7,247 | 132 | 342 |
| `functional-graphs-quick` | 42,356 | 37,151 | 37,112 | 39,672 | 34,467 | 34,428 | 1,848 | 836 |
| `gcd-bezout` | 5,262 | 4,992 | 4,992 | 4,950 | 4,680 | 4,680 | 46 | 266 |
| `geometry-primitives-boxes-and-angles` | 24,029 | 21,867 | 21,828 | 23,208 | 21,046 | 21,007 | 365 | 456 |
| `geometry-primitives-quick` | 18,007 | 16,241 | 16,202 | 17,437 | 15,671 | 15,632 | 190 | 380 |
| `geometry-primitives-segments-and-lines` | 28,754 | 25,513 | 25,474 | 27,995 | 24,754 | 24,715 | 303 | 456 |
| `graph-callbacks` | 27,028 | 24,328 | 24,288 | 25,625 | 22,925 | 22,885 | 833 | 570 |
| `graph-components-blocks` | 26,001 | 23,764 | 23,725 | 23,910 | 21,673 | 21,634 | 1,369 | 722 |
| `graph-components-bridges` | 24,591 | 22,344 | 22,305 | 22,499 | 20,252 | 20,213 | 1,370 | 722 |
| `graph-components-scc` | 23,304 | 21,265 | 21,226 | 21,212 | 19,173 | 19,134 | 1,370 | 722 |
| `graph-quick` | 27,192 | 24,768 | 24,729 | 25,195 | 22,771 | 22,732 | 1,313 | 684 |
| `halfplane-intersection-quick` | 56,612 | 48,141 | 48,102 | 55,163 | 46,692 | 46,653 | 841 | 608 |
| `hierarchical-bitset-basic` | 4,496 | 4,245 | 4,206 | 4,325 | 4,074 | 4,035 | 19 | 152 |
| `int128-arithmetic` | 561 | 558 | 558 | 429 | 426 | 426 | 18 | 114 |
| `lazy-segment-tree-quick-start` | 9,645 | 8,735 | 8,696 | 9,217 | 8,307 | 8,268 | 238 | 190 |
| `lca-path-sum` | 24,004 | 22,124 | 22,085 | 21,858 | 19,978 | 19,939 | 1,424 | 722 |
| `lca-quick` | 18,269 | 16,828 | 16,789 | 16,622 | 15,181 | 15,142 | 1,039 | 608 |
| `lexicographic-order-quick` | 8,442 | 8,030 | 7,991 | 7,935 | 7,523 | 7,484 | 279 | 228 |
| `li-chao-tree-basic` | 22,104 | 19,210 | 19,171 | 21,282 | 18,388 | 18,349 | 366 | 456 |
| `linear-recurrence-quick` | 75,796 | 70,687 | 70,648 | 73,344 | 68,235 | 68,196 | 1,464 | 988 |
| `link-cut-tree-basic` | 75,025 | 67,124 | 67,085 | 73,072 | 65,171 | 65,132 | 1,193 | 760 |
| `lyndon-quick` | 19,532 | 18,486 | 18,447 | 18,363 | 17,317 | 17,278 | 675 | 494 |
| `manhattan-mst-quick` | 11,256 | 10,415 | 10,376 | 10,578 | 9,737 | 9,698 | 260 | 418 |
| `matching-quick` | 5,849 | 5,530 | 5,491 | 5,401 | 5,082 | 5,043 | 220 | 228 |
| `matrix-quick` | 46,405 | 42,841 | 42,802 | 45,079 | 41,515 | 41,476 | 604 | 722 |
| `max-flow-bounds` | 21,067 | 19,076 | 19,037 | 20,591 | 18,600 | 18,561 | 210 | 266 |
| `max-flow-quick` | 14,873 | 13,595 | 13,556 | 14,396 | 13,118 | 13,079 | 211 | 266 |
| `meldable-heap-forest-basic` | 10,737 | 9,634 | 9,595 | 10,463 | 9,360 | 9,321 | 84 | 190 |
| `min-cost-flow-quick` | 25,688 | 23,476 | 23,437 | 24,978 | 22,766 | 22,727 | 406 | 304 |
| `min-cost-flow-supplies` | 21,788 | 19,933 | 19,894 | 21,078 | 19,223 | 19,184 | 406 | 304 |
| `modular-arithmetic-residues` | 36,000 | 33,818 | 33,779 | 34,942 | 32,760 | 32,721 | 374 | 684 |
| `multiplicative-functions-quick` | 10,992 | 10,221 | 10,182 | 10,191 | 9,420 | 9,381 | 345 | 456 |
| `online-convolution-partitions` | 73,676 | 68,373 | 68,334 | 70,230 | 64,927 | 64,888 | 2,154 | 1,292 |
| `online-convolution-quick` | 73,245 | 68,028 | 67,989 | 69,799 | 64,582 | 64,543 | 2,154 | 1,292 |
| `palindrome-lengths-quick` | 3,786 | 3,618 | 3,579 | 3,339 | 3,171 | 3,132 | 219 | 228 |
| `palindromic-tree-quick` | 12,700 | 11,803 | 11,764 | 11,642 | 10,745 | 10,706 | 602 | 456 |
| `persistent-array-versions` | 17,943 | 16,244 | 16,205 | 17,183 | 15,484 | 15,445 | 342 | 418 |
| `persistent-heap-basic` | 14,667 | 13,326 | 13,287 | 13,893 | 12,552 | 12,513 | 356 | 418 |
| `persistent-lazy-segment-tree-basic` | 17,092 | 15,035 | 14,996 | 16,492 | 14,435 | 14,396 | 296 | 304 |
| `persistent-list-basic` | 9,899 | 9,132 | 9,093 | 9,540 | 8,773 | 8,734 | 131 | 228 |
| `persistent-queue-deque-basic` | 24,819 | 22,615 | 22,576 | 23,777 | 21,573 | 21,534 | 548 | 494 |
| `persistent-segment-tree-basic` | 10,732 | 9,745 | 9,706 | 10,110 | 9,123 | 9,084 | 318 | 304 |
| `persistent-treap-sequence-basic` | 33,920 | 30,836 | 30,797 | 32,681 | 29,597 | 29,558 | 669 | 570 |
| `persistent-vector-basic` | 36,714 | 32,676 | 32,637 | 35,957 | 31,919 | 31,880 | 339 | 418 |
| `point-location-quick` | 84,012 | 75,934 | 75,894 | 81,445 | 73,367 | 73,327 | 1,655 | 912 |
| `polygon-overlay-quick` | 75,060 | 67,625 | 67,585 | 72,886 | 65,451 | 65,411 | 1,338 | 836 |
| `polygon-quick` | 29,353 | 26,231 | 26,192 | 28,574 | 25,452 | 25,413 | 323 | 456 |
| `polynomial-gcd-pade` | 85,976 | 77,474 | 77,435 | 82,457 | 73,955 | 73,916 | 2,227 | 1,292 |
| `polynomial-gcd-quick` | 87,739 | 79,033 | 78,994 | 84,239 | 75,533 | 75,494 | 2,208 | 1,292 |
| `polynomial-points-quick` | 92,407 | 83,130 | 83,091 | 89,075 | 79,798 | 79,759 | 2,078 | 1,254 |
| `polynomial-points-sampling` | 77,988 | 71,525 | 71,486 | 74,656 | 68,193 | 68,154 | 2,078 | 1,254 |
| `power-monoid` | 26,758 | 25,165 | 25,126 | 25,852 | 24,259 | 24,220 | 298 | 608 |
| `power-series-powers-and-roots` | 88,938 | 79,890 | 79,851 | 85,992 | 76,944 | 76,905 | 1,806 | 1,140 |
| `power-series-prime-field-helpers` | 42,572 | 39,267 | 39,228 | 40,719 | 37,414 | 37,375 | 903 | 950 |
| `power-series-quick` | 73,954 | 68,220 | 68,181 | 71,008 | 65,274 | 65,235 | 1,806 | 1,140 |
| `radial-sweep-quick` | 14,693 | 13,483 | 13,444 | 14,068 | 12,858 | 12,819 | 245 | 380 |
| `rmq-basic` | 6,501 | 6,109 | 6,070 | 6,093 | 5,701 | 5,662 | 66 | 342 |
| `segment-intersections-quick` | 31,927 | 28,447 | 28,408 | 31,086 | 27,606 | 27,567 | 347 | 494 |
| `segment-tree-beats-quick-start` | 22,926 | 21,115 | 21,076 | 22,106 | 20,295 | 20,256 | 364 | 456 |
| `segment-tree-quick-start` | 7,350 | 6,824 | 6,785 | 6,905 | 6,379 | 6,340 | 255 | 190 |
| `series-composition-quick` | 110,670 | 97,330 | 97,291 | 106,910 | 93,570 | 93,531 | 2,392 | 1,368 |
| `sieve-quick` | 8,039 | 7,440 | 7,401 | 7,692 | 7,093 | 7,054 | 119 | 228 |
| `sorted-dictionary-quick` | 18,873 | 17,823 | 17,784 | 17,872 | 16,822 | 16,783 | 545 | 456 |
| `sparse-table-basic` | 5,573 | 5,239 | 5,200 | 5,322 | 4,988 | 4,949 | 99 | 152 |
| `splay-engine-quick-start` | 11,156 | 10,069 | 10,030 | 10,964 | 9,877 | 9,838 | 40 | 152 |
| `splay-map-quick-start` | 38,919 | 35,014 | 34,975 | 37,033 | 33,128 | 33,089 | 1,126 | 760 |
| `splay-sequence-quick-start` | 37,826 | 33,485 | 33,446 | 37,061 | 32,720 | 32,681 | 423 | 342 |
| `static-trie-quick` | 14,401 | 13,608 | 13,569 | 13,110 | 12,317 | 12,278 | 797 | 494 |
| `static-vector-basic` | 4,854 | 4,576 | 4,537 | 4,680 | 4,402 | 4,363 | 60 | 114 |
| `string-hash-quick` | 17,855 | 16,719 | 16,680 | 16,644 | 15,508 | 15,469 | 603 | 608 |
| `subtree-link-cut-tree-basic` | 79,767 | 71,487 | 71,448 | 77,677 | 69,397 | 69,358 | 1,292 | 798 |
| `suffix-array-quick` | 7,615 | 7,218 | 7,179 | 7,129 | 6,732 | 6,693 | 258 | 228 |
| `suffix-automaton-quick` | 15,008 | 13,949 | 13,910 | 13,968 | 12,909 | 12,870 | 584 | 456 |
| `suffix-tree-quick` | 17,620 | 16,201 | 16,162 | 16,580 | 15,161 | 15,122 | 584 | 456 |
| `time-divide-and-conquer-connectivity` | 45,498 | 41,508 | 41,469 | 44,109 | 40,119 | 40,080 | 819 | 570 |
| `top-tree-basic` | 110,519 | 98,147 | 98,108 | 108,528 | 96,156 | 96,117 | 1,193 | 798 |
| `treap-map-quick-start` | 29,934 | 27,048 | 27,009 | 28,551 | 25,665 | 25,626 | 737 | 646 |
| `treap-sequence-quick-start` | 28,888 | 26,021 | 25,982 | 27,759 | 24,892 | 24,853 | 597 | 532 |
| `triangulation-quick` | 64,873 | 59,082 | 59,043 | 63,079 | 57,288 | 57,249 | 1,072 | 722 |
| `trie-quick` | 12,510 | 11,717 | 11,678 | 11,644 | 10,851 | 10,812 | 448 | 418 |
| `types-and-utilities-composite-key` | 11,364 | 10,586 | 10,586 | 10,953 | 10,175 | 10,175 | 145 | 266 |
| `virtual-tree-quick` | 23,547 | 21,637 | 21,598 | 21,681 | 19,771 | 19,732 | 1,220 | 646 |
| `virtual-tree-separation` | 24,159 | 22,165 | 22,126 | 22,259 | 20,265 | 20,226 | 1,254 | 646 |
| `wavelet-matrix-basic` | 10,544 | 9,961 | 9,922 | 9,945 | 9,362 | 9,323 | 219 | 380 |
| `weighted-dsu-matrix` | 32,471 | 30,117 | 30,078 | 31,328 | 28,974 | 28,935 | 459 | 684 |
| `weighted-dsu-potentials` | 38,097 | 34,924 | 34,885 | 36,645 | 33,472 | 33,433 | 692 | 760 |
| `xor-basis-queries` | 7,372 | 6,967 | 6,928 | 6,924 | 6,519 | 6,480 | 106 | 342 |
| **Total** | **3,811,229** | **3,461,651** | **3,456,885** | **3,655,539** | **3,305,961** | **3,301,195** | **87,024** | **68,666** |
