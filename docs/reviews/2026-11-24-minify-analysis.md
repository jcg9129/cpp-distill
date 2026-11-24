# Spelling-only source-size analysis (2026-11-24)

Analysis only; no production changes. Base `origin/main` / `6205039`, branch
`minify-analysis`. The 126 ATL examples are revision `5b14f2d`, judge `cses`,
distill main with D3f, from the specified read-only comparison shards.

T1+T2 saves **1,232,510 B (32.34%)** in aggregate. The count over
65,536 B falls from **17 to 2**.
Adding the parallel include/define cleanup estimate saves **1,384,328 B
(36.32%)** in aggregate; the same two examples still exceed 64 KiB.
Shared header text is counted once per output occurrence in these totals.

## Totals and source-size limits

“Over” means strictly greater than the stated cap; an output exactly at the cap fits.
R means repeated standard includes plus unused `DISTILL_ONCE_*` defines removed.

| Measure | Compact | T1 | T2 | T1+T2 | T1+T2+R |
|---|---:|---:|---:|---:|---:|
| Total bytes | 3,811,229 | 3,452,529 | 2,937,419 | 2,578,719 | 2,426,901 |
| Bytes saved from compact | 0 | 358,700 | 873,810 | 1,232,510 | 1,384,328 |
| Reduction | 0.00% | 9.41% | 22.93% | 32.34% | 36.32% |
| Examples over 65,536 B | 17 | 16 | 6 | 2 | 2 |
| Examples over 50,000 B | 23 | 23 | 18 | 13 | 9 |
| Examples over 32,768 B | 41 | 35 | 28 | 23 | 22 |

Still over 65,536 B after T1+T2:

| Example | T1+T2 bytes | T1+T2+R bytes |
|---|---:|---:|
| `series-composition-quick` | 72,076 | 68,427 |
| `top-tree-basic` | 69,116 | 67,173 |

## Method and provenance

All 126 compact sizes match the `distill-compact` rows of the supplied `summary.tsv`.
Every `.rep` says pruned, records A6/A12/D3f, and passes the source-position
observation gate for compaction. In particular, this newer `fast-io-roundtrip`
artifact passes; the older fallback discussed in the trim review is not this input.
Input and report hashes are in [input-manifest.tsv](minify-analysis/input-manifest.tsv).

**T1:** tokenize C++ preprocessing tokens, then remove inter-token whitespace unless
it is needed to preserve the token sequence, a directive boundary, a line comment,
or object-like versus function-like macro definition. Context-sensitive cases such
as `< :: >`, `. . .`, literal prefixes/suffixes and accidental comments are checked.
String, character and raw-string literal bytes are preserved exactly. Comments are
preserved too. Following the trim review's [Formatting rules](2026-11-22-trim-analysis.md#2-formatting-after-source-position-checks),
the 366 existing backslash-newline continuations are retained; the
corpus has no splices inside tokens. Directive bodies are compacted while retaining
their logical-line boundary. This is a lexer-minimal counterfactual under these
preservation constraints, not a production compaction change.
If phase-2 splice erasure is counted as whitespace normalization as well, subtract
732 B from the T1, T1+T2 and T1+T2+R totals: respectively
3,451,797, 2,577,987 and
2,426,169 B. All over-limit counts stay the same.

**T2:** libclang visits declarations whose expansion location is the user source,
including namespaces, types, aliases, concepts, members, template parameters,
enumerators, parameters and local variables. This is not a regex guess that every
unknown reference is user-declared. Replacement is simultaneous and global by
spelling within each output, including references and ordinary macro bodies.
An injective mapping gives the shortest available names to descending occurrence
counts; frequency ties are alphabetical. Names use ASCII letters first, then letters,
digits and underscores; generated names never start with an underscore or contain
`__`. All unrenamed source spellings, library spellings and keywords are blocked.
Already-short eligible old names can be reassigned, rather than unnecessarily
blocking those names. No profitability cutoff biases the frequency assignment.
The result assigns 20,263
per-example spellings (20,242 actually change); all names
fit in one or two bytes. T2 saves **873,810 B**. Per-spelling counts and maps
are retained in ignored `measurements/minify-analysis/renames.json`.

Exclusions are C++ keywords and alternative tokens (also conservative contextual
keyword spellings), identifiers in included library headers, every A6 registry name,
`main`, all defined macro names and parameters, every identifier in a replacement
list containing `#` or `##`, identifiers in string-literal contents, attribute names,
and reserved identifiers (including `__` anywhere). Ordinary octal/hex string escapes
are also decoded for the string-content exclusion. Literal suffixes are preserved;
there are no user literal-operator declarations or numeric user-defined suffixes
in this corpus. Attribute payloads here contain only `nodiscard`, `no_unique_address`,
`maybe_unused` and `unlikely`, so preserving their identifiers adds no argument-name
restriction. `std` stays library-spelled; `atl` is renamed where eligible.

The library approximation uses Debian's **libstdc++-13-dev 13.3.0-16, arm64**,
source package `gcc-13`: [downloaded binary package](https://deb.debian.org/debian/pool/main/g/gcc-13/libstdc++-13-dev_13.3.0-16_arm64.deb).
SHA-256: `5db1835e949dc2682ee27f3732af709bc6a80f9a825a70a389dd84f24ed48078`.
It was unpacked under ignored `measurements/minify-analysis/gcc13/`; nothing was
installed or changed in system headers. For each TU, Clang records the actual
transitive include graph using those headers; Clang raw-tokenizes each reached
header in full, including inactive branches and macro definitions, and unions its
identifier spellings. Raw scanning conservatively counts identifier components of
header operands too. The 363 distinct files include host libc and
Clang builtin headers; [header-manifest.tsv](minify-analysis/header-manifest.tsv)
records every hash. This is a native AArch64 approximation, not a capture of CSES's
x86-64 environment or the multi-build A12 inventory. Macro-generated names absent
from raw spelling and alternative target-selected headers remain limitations.

**R:** retain the first identical angle-bracket standard include in textual order;
count each later entire physical line. Remove an empty `DISTILL_ONCE_*` definition
only when its name has no other identifier-token occurrence anywhere in the output.
All 6995 includes in these inputs name standard headers;
4446 later occurrences account for **87,024 B**.
The 1807 unused once-defines account for **68,666 B**.
These are original-line byte counts. The extra saving after T1+T2 is
**151,818 B**; it is not their raw sum because T1 has
already removed spaces and some directive-separating newlines become unnecessary.
The combined column removes those lines, keeps the original T2 mapping, and reruns
T1; it does not reoptimize name allocation after cleanup. This is a size estimate
of the parallel rules, not a general proof that repeated inclusion has no effects
under intervening macro changes.

## Verification

**504/504 syntax checks pass**: all 126 examples in each of T1, T2, both, and both+R, using GCC 15.2 with the downloaded GCC 13 headers and `-std=c++23 -fsyntax-only`. **378/378 independent Clang lexer comparisons pass**: original versus T1, and the expected identifier substitution versus T2 and both. The checks normalize phase-2 line splices outside literal bodies; literal bytes are also checked exactly by the measurement scanner. **12/12 focused tests pass**, covering macro adjacency and parameters, directives, comments, header operands, literal preservation, digraph / ellipsis boundaries, name assignment and cleanup accounting. These are syntax and token checks, not runtime equivalence or judge certification. No sample-input files are part of the authorized input set.

Reproduction commands and dependencies are in [the script README](minify-analysis/README.md).
Machine-readable measurements are [results.tsv](minify-analysis/results.tsv) and
[results.json](minify-analysis/results.json); checks are in
[verification.json](minify-analysis/verification.json) and
[token-verification.json](minify-analysis/token-verification.json).

## Soundness limits

The design's [A6, A12 and name rule](../design.md) are retention rules: the name
rule treats kept identifier spellings and literal suffixes as dependencies. They
do not authorize renaming. The broad exclusions preserve many lookup protocols,
but this remains a counterfactual size bound rather than a sound general renamer.

| Mechanism | What the exclusions cover, and what remains |
|---|---|
| Token pasting / stringizing | Macro names, parameters and all identifiers in a `#`/`##` body stay. Invocation arguments can still be renamed or respaced, changing synthesized names or strings; indirect library macros can do this too. GCC documents whitespace-sensitive [stringizing](https://gcc.gnu.org/onlinedocs/gcc-13.3.0/cpp/Stringizing.html). No user replacement list here contains either operator; `atl_assert(condition)` is retained with its parameter. |
| Dependent lookup and ADL | User-only spelling equalities are preserved by global injective replacement. A6 keeps `swap`, `begin/end`, `get`, etc.; the header union also protects library-written dependent member/type names. A different target's headers, token-generated names or unsurveyed dispatch paths can escape the approximation. ATL range adapters and `calipers-quick` use range / swap protocols. |
| Members called by the library; using library names | Raw library-spelled identifiers stay everywhere, including unrelated ATL homonyms. This protects library callbacks, dependent probes, overrides and `using std::name` from partial renaming. It mirrors A12's intent but omits its multi-build / expansion union. `persistent-segment-tree-basic` uses library range traits and member `size`; `graph-callbacks` has tuple / dependent callback probes. |
| Specializations and structured bindings | `std`, `hash`, `formatter`, `tuple_size`, `tuple_element`, `value`, `type` and `get` stay when library-spelled; A6 independently keeps `get`. `hash::operator()` needs no identifier rename for `operator()` itself. `types-and-utilities-composite-key` demonstrates ATL's own `hash` and call operators; it is not a `std::hash` specialization. `graph-callbacks` uses `std::tuple_size`/`std::get`; no written `std` specialization was found. |
| Iterator typedefs and language protocols | Library spelling protects `value_type`, `difference_type`, `iterator_category`, etc.; A6 protects range-for and structured-binding hooks. `subtree-link-cut-tree-basic` has `iterator_category` and `value_type`; `value_type` occurs in 81 outputs. Compiler-only protocols outside the inventory would need explicit protection. |
| Operators and literal suffixes | Keyword / punctuation operator names stay; renamed constructor, destructor and conversion type tokens follow the same spelling map. A general implementation must treat UDL suffixes as names even inside pp-number/literal tokens, preserving or rewriting declarations and uses together. No user UDL occurs here, so literal preservation loses no measured opportunity. |
| Designated initializers and attributes | `.field` and the user field declaration are renamed together; library fields stay through library spelling. Attribute names/namespaces stay. Identifier-like content inside compiler-interpreted strings, assembly or mangled linkage names is not fully understood by this spelling model. |
| Observable names and reflection-like tricks | Keeping string identifiers catches direct textual name comparisons, but constructed/concatenated strings can hide them. Keeping reserved `__func__`, `__PRETTY_FUNCTION__`, etc. does not preserve their generated values after renaming; `typeid(T).name()`, `source_location::function_name()` and dump/serialization tricks can also expose changed function/type/member names. No direct `__func__`, `typeid` or pretty-function token appears here. External ABI / symbol lookup is not covered either. |

Removing whitespace also changes physical source locations and may change stringized
arguments despite token equality. All current reports pass the existing position
gate, but a production implementation would still need that gate and separate
macro / observable-name checks. Successful compilation does not prove those effects absent.

## Per-example bytes

The two R columns count bytes in original lines. “T2 saved” is compact minus T2.

| Example | Compact | T1 | T2 | T2 saved | T1+T2 | Repeated includes | Unused once-defines | T1+T2+R |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `aggregate-queue-deque-sliding-window` | 9,963 | 9,158 | 7,489 | 2,474 | 6,684 | 241 | 190 | 6,263 |
| `aho-corasick-quick` | 19,355 | 17,696 | 14,972 | 4,383 | 13,313 | 924 | 570 | 11,862 |
| `arrangement-quick` | 68,155 | 61,774 | 52,268 | 15,887 | 45,887 | 1,244 | 798 | 43,900 |
| `assignment-quick` | 10,789 | 9,521 | 8,464 | 2,325 | 7,196 | 155 | 228 | 6,821 |
| `bipartite-matching-dag` | 34,885 | 31,790 | 27,041 | 7,844 | 23,946 | 1,534 | 798 | 21,684 |
| `bipartite-matching-quick` | 33,115 | 30,294 | 25,595 | 7,520 | 22,774 | 1,534 | 798 | 20,512 |
| `bit-utilities-combinations` | 2,369 | 2,181 | 1,988 | 381 | 1,800 | 46 | 304 | 1,450 |
| `bitwise-convolution-quick` | 37,121 | 33,442 | 30,808 | 6,313 | 27,129 | 407 | 646 | 26,090 |
| `calipers-quick` | 46,506 | 41,983 | 33,235 | 13,271 | 28,712 | 670 | 532 | 27,542 |
| `centroid-decomposition-quick` | 33,036 | 30,172 | 25,234 | 7,802 | 22,370 | 1,676 | 760 | 20,011 |
| `circle-quick` | 49,780 | 44,625 | 36,721 | 13,059 | 31,566 | 997 | 684 | 29,927 |
| `closest-pair-quick` | 12,260 | 11,177 | 9,790 | 2,470 | 8,707 | 222 | 380 | 8,114 |
| `convex-hull-quick` | 29,686 | 26,698 | 21,963 | 7,723 | 18,975 | 536 | 532 | 17,931 |
| `convex-piecewise-linear-forest-quick` | 24,606 | 22,529 | 17,369 | 7,237 | 15,292 | 527 | 456 | 14,331 |
| `convex-piecewise-linear-moving` | 15,413 | 14,041 | 11,022 | 4,391 | 9,650 | 191 | 342 | 9,124 |
| `convex-piecewise-linear-quick` | 12,995 | 11,852 | 9,500 | 3,495 | 8,357 | 191 | 342 | 7,831 |
| `convex-polygon-operations-quick` | 74,297 | 66,676 | 53,177 | 21,120 | 45,556 | 991 | 684 | 43,927 |
| `convolution-quick` | 60,073 | 53,693 | 49,914 | 10,159 | 43,534 | 1,039 | 874 | 41,669 |
| `coordinate-compression-basic` | 17,535 | 16,295 | 14,250 | 3,285 | 13,010 | 287 | 418 | 12,315 |
| `coordinate-compression-move-only` | 17,794 | 16,525 | 14,459 | 3,335 | 13,190 | 328 | 418 | 12,456 |
| `counting-sort-stable-records` | 4,120 | 3,731 | 3,337 | 783 | 2,948 | 0 | 152 | 2,796 |
| `crt-quick` | 13,043 | 11,881 | 10,591 | 2,452 | 9,429 | 127 | 456 | 8,850 |
| `delaunay-quick` | 62,229 | 55,962 | 45,630 | 16,599 | 39,363 | 1,836 | 912 | 36,699 |
| `difference-fenwick-basic` | 14,441 | 13,276 | 11,741 | 2,700 | 10,576 | 385 | 418 | 9,787 |
| `discrete-log-quick` | 61,410 | 55,508 | 49,701 | 11,709 | 43,799 | 1,243 | 912 | 41,699 |
| `dp-optimization-interval-merge` | 9,528 | 8,669 | 7,602 | 1,926 | 6,743 | 352 | 418 | 5,985 |
| `dp-optimization-partition` | 14,672 | 13,275 | 10,348 | 4,324 | 8,951 | 394 | 418 | 8,154 |
| `dp-optimization-quick` | 7,784 | 7,073 | 6,217 | 1,567 | 5,506 | 353 | 418 | 4,747 |
| `dp-optimization-row-minima` | 7,724 | 6,960 | 5,648 | 2,076 | 4,884 | 234 | 380 | 4,281 |
| `dsu-history` | 19,137 | 17,590 | 15,337 | 3,800 | 13,790 | 570 | 532 | 12,711 |
| `dual-segment-tree-quick-start` | 9,164 | 8,386 | 6,883 | 2,281 | 6,105 | 200 | 190 | 5,724 |
| `dynamic-lazy-segment-tree-quick-start` | 9,363 | 8,371 | 7,191 | 2,172 | 6,199 | 61 | 190 | 5,951 |
| `dynamic-segment-tree-quick-start` | 5,715 | 5,079 | 4,454 | 1,261 | 3,818 | 61 | 190 | 3,570 |
| `euler-tour-tree-basic` | 56,402 | 51,359 | 44,221 | 12,181 | 39,178 | 1,107 | 760 | 37,355 |
| `exact-count-optimum-quick` | 19,060 | 17,248 | 13,879 | 5,181 | 12,067 | 616 | 532 | 10,945 |
| `factorization-quick` | 21,361 | 19,128 | 17,668 | 3,693 | 15,435 | 378 | 646 | 14,427 |
| `fast-io-roundtrip` | 39,558 | 35,487 | 32,703 | 6,855 | 28,632 | 617 | 722 | 27,318 |
| `fenwick-tree-basic` | 13,612 | 12,524 | 10,722 | 2,890 | 9,634 | 228 | 380 | 9,033 |
| `flat-hash-basic` | 33,436 | 30,741 | 27,360 | 6,076 | 24,665 | 699 | 532 | 23,464 |
| `floor-sum-quick` | 8,773 | 7,731 | 6,199 | 2,574 | 5,157 | 132 | 342 | 4,685 |
| `functional-graphs-quick` | 42,356 | 38,868 | 31,225 | 11,131 | 27,737 | 1,848 | 836 | 25,140 |
| `gcd-bezout` | 5,262 | 4,835 | 4,425 | 837 | 3,998 | 46 | 266 | 3,686 |
| `geometry-primitives-boxes-and-angles` | 24,029 | 21,597 | 18,566 | 5,463 | 16,134 | 365 | 456 | 15,328 |
| `geometry-primitives-quick` | 18,007 | 16,174 | 13,436 | 4,571 | 11,603 | 190 | 380 | 11,040 |
| `geometry-primitives-segments-and-lines` | 28,754 | 25,714 | 21,207 | 7,547 | 18,167 | 303 | 456 | 17,421 |
| `graph-callbacks` | 27,028 | 25,047 | 19,426 | 7,602 | 17,445 | 833 | 570 | 16,081 |
| `graph-components-blocks` | 26,001 | 24,065 | 20,089 | 5,912 | 18,153 | 1,369 | 722 | 16,126 |
| `graph-components-bridges` | 24,591 | 22,832 | 18,849 | 5,742 | 17,090 | 1,370 | 722 | 15,061 |
| `graph-components-scc` | 23,304 | 21,612 | 18,037 | 5,267 | 16,345 | 1,370 | 722 | 14,316 |
| `graph-quick` | 27,192 | 25,188 | 20,877 | 6,315 | 18,873 | 1,313 | 684 | 16,937 |
| `halfplane-intersection-quick` | 56,612 | 51,168 | 39,063 | 17,549 | 33,619 | 841 | 608 | 32,209 |
| `hierarchical-bitset-basic` | 4,496 | 3,897 | 3,814 | 682 | 3,215 | 19 | 152 | 3,045 |
| `int128-arithmetic` | 561 | 521 | 511 | 50 | 471 | 18 | 114 | 340 |
| `lazy-segment-tree-quick-start` | 9,645 | 8,741 | 7,491 | 2,154 | 6,587 | 238 | 190 | 6,170 |
| `lca-path-sum` | 24,004 | 21,800 | 19,376 | 4,628 | 17,172 | 1,424 | 722 | 15,093 |
| `lca-quick` | 18,269 | 16,521 | 14,497 | 3,772 | 12,749 | 1,039 | 608 | 11,149 |
| `lexicographic-order-quick` | 8,442 | 7,677 | 7,055 | 1,387 | 6,290 | 279 | 228 | 5,797 |
| `li-chao-tree-basic` | 22,104 | 20,091 | 15,833 | 6,271 | 13,820 | 366 | 456 | 13,013 |
| `linear-recurrence-quick` | 75,796 | 67,766 | 62,270 | 13,526 | 54,240 | 1,464 | 988 | 51,856 |
| `link-cut-tree-basic` | 75,025 | 68,897 | 54,428 | 20,597 | 48,300 | 1,193 | 760 | 46,397 |
| `lyndon-quick` | 19,532 | 17,292 | 15,949 | 3,583 | 13,709 | 675 | 494 | 12,569 |
| `manhattan-mst-quick` | 11,256 | 10,249 | 9,144 | 2,112 | 8,137 | 260 | 418 | 7,469 |
| `matching-quick` | 5,849 | 5,270 | 4,804 | 1,045 | 4,225 | 220 | 228 | 3,787 |
| `matrix-quick` | 46,405 | 41,706 | 36,764 | 9,641 | 32,065 | 604 | 722 | 30,764 |
| `max-flow-bounds` | 21,067 | 19,118 | 15,575 | 5,492 | 13,626 | 210 | 266 | 13,160 |
| `max-flow-quick` | 14,873 | 13,468 | 11,269 | 3,604 | 9,864 | 211 | 266 | 9,397 |
| `meldable-heap-forest-basic` | 10,737 | 9,729 | 7,819 | 2,918 | 6,811 | 84 | 190 | 6,541 |
| `min-cost-flow-quick` | 25,688 | 23,183 | 19,333 | 6,355 | 16,828 | 406 | 304 | 16,138 |
| `min-cost-flow-supplies` | 21,788 | 19,674 | 16,389 | 5,399 | 14,275 | 406 | 304 | 13,585 |
| `modular-arithmetic-residues` | 36,000 | 32,303 | 29,155 | 6,845 | 25,458 | 374 | 684 | 24,412 |
| `multiplicative-functions-quick` | 10,992 | 10,001 | 9,096 | 1,896 | 8,105 | 345 | 456 | 7,319 |
| `online-convolution-partitions` | 73,676 | 66,128 | 59,783 | 13,893 | 52,235 | 2,154 | 1,292 | 48,892 |
| `online-convolution-quick` | 73,245 | 65,740 | 59,501 | 13,744 | 51,996 | 2,154 | 1,292 | 48,653 |
| `palindrome-lengths-quick` | 3,786 | 3,392 | 3,245 | 541 | 2,851 | 219 | 228 | 2,414 |
| `palindromic-tree-quick` | 12,700 | 11,582 | 10,116 | 2,584 | 8,998 | 602 | 456 | 7,968 |
| `persistent-array-versions` | 17,943 | 16,440 | 14,115 | 3,828 | 12,612 | 342 | 418 | 11,865 |
| `persistent-heap-basic` | 14,667 | 13,441 | 11,333 | 3,334 | 10,107 | 356 | 418 | 9,351 |
| `persistent-lazy-segment-tree-basic` | 17,092 | 15,564 | 12,285 | 4,807 | 10,757 | 296 | 304 | 10,171 |
| `persistent-list-basic` | 9,899 | 9,023 | 7,766 | 2,133 | 6,890 | 131 | 228 | 6,538 |
| `persistent-queue-deque-basic` | 24,819 | 22,654 | 19,796 | 5,023 | 17,631 | 548 | 494 | 16,611 |
| `persistent-segment-tree-basic` | 10,732 | 9,745 | 8,516 | 2,216 | 7,529 | 318 | 304 | 6,922 |
| `persistent-treap-sequence-basic` | 33,920 | 30,760 | 25,325 | 8,595 | 22,165 | 669 | 570 | 20,953 |
| `persistent-vector-basic` | 36,714 | 33,052 | 26,857 | 9,857 | 23,195 | 339 | 418 | 22,451 |
| `point-location-quick` | 84,012 | 75,918 | 64,027 | 19,985 | 55,933 | 1,655 | 912 | 53,440 |
| `polygon-overlay-quick` | 75,060 | 68,128 | 56,900 | 18,160 | 49,968 | 1,338 | 836 | 47,853 |
| `polygon-quick` | 29,353 | 26,328 | 22,022 | 7,331 | 18,997 | 323 | 456 | 18,232 |
| `polynomial-gcd-pade` | 85,976 | 77,619 | 66,652 | 19,324 | 58,295 | 2,227 | 1,292 | 54,882 |
| `polynomial-gcd-quick` | 87,739 | 79,297 | 67,719 | 20,020 | 59,277 | 2,208 | 1,292 | 55,882 |
| `polynomial-points-quick` | 92,407 | 83,372 | 71,654 | 20,753 | 62,619 | 2,078 | 1,254 | 59,384 |
| `polynomial-points-sampling` | 77,988 | 70,135 | 62,231 | 15,757 | 54,378 | 2,078 | 1,254 | 51,144 |
| `power-monoid` | 26,758 | 24,123 | 22,087 | 4,671 | 19,452 | 298 | 608 | 18,555 |
| `power-series-powers-and-roots` | 88,938 | 80,272 | 68,900 | 20,038 | 60,234 | 1,806 | 1,140 | 57,372 |
| `power-series-prime-field-helpers` | 42,572 | 38,456 | 34,591 | 7,981 | 30,475 | 903 | 950 | 28,660 |
| `power-series-quick` | 73,954 | 66,496 | 59,657 | 14,297 | 52,199 | 1,806 | 1,140 | 49,339 |
| `radial-sweep-quick` | 14,693 | 13,282 | 11,431 | 3,262 | 10,020 | 245 | 380 | 9,405 |
| `rmq-basic` | 6,501 | 5,912 | 5,264 | 1,237 | 4,675 | 66 | 342 | 4,268 |
| `segment-intersections-quick` | 31,927 | 28,662 | 23,454 | 8,473 | 20,189 | 347 | 494 | 19,362 |
| `segment-tree-beats-quick-start` | 22,926 | 20,726 | 18,310 | 4,616 | 16,110 | 364 | 456 | 15,303 |
| `segment-tree-quick-start` | 7,350 | 6,640 | 6,162 | 1,188 | 5,452 | 255 | 190 | 5,020 |
| `series-composition-quick` | 110,670 | 99,943 | 82,803 | 27,867 | 72,076 | 2,392 | 1,368 | 68,427 |
| `sieve-quick` | 8,039 | 7,278 | 6,655 | 1,384 | 5,894 | 119 | 228 | 5,553 |
| `sorted-dictionary-quick` | 18,873 | 17,054 | 15,524 | 3,349 | 13,705 | 545 | 456 | 12,726 |
| `sparse-table-basic` | 5,573 | 5,004 | 4,671 | 902 | 4,102 | 99 | 152 | 3,856 |
| `splay-engine-quick-start` | 11,156 | 9,996 | 7,784 | 3,372 | 6,624 | 40 | 152 | 6,434 |
| `splay-map-quick-start` | 38,919 | 35,409 | 28,572 | 10,347 | 25,062 | 1,126 | 760 | 23,225 |
| `splay-sequence-quick-start` | 37,826 | 34,429 | 26,973 | 10,853 | 23,576 | 423 | 342 | 22,830 |
| `static-trie-quick` | 14,401 | 13,071 | 11,615 | 2,786 | 10,285 | 797 | 494 | 9,030 |
| `static-vector-basic` | 4,854 | 4,420 | 4,259 | 595 | 3,825 | 60 | 114 | 3,654 |
| `string-hash-quick` | 17,855 | 16,285 | 14,416 | 3,439 | 12,846 | 603 | 608 | 11,662 |
| `subtree-link-cut-tree-basic` | 79,767 | 73,214 | 58,196 | 21,571 | 51,643 | 1,292 | 798 | 49,608 |
| `suffix-array-quick` | 7,615 | 6,614 | 6,214 | 1,401 | 5,213 | 258 | 228 | 4,740 |
| `suffix-automaton-quick` | 15,008 | 13,630 | 11,776 | 3,232 | 10,398 | 584 | 456 | 9,386 |
| `suffix-tree-quick` | 17,620 | 15,992 | 13,080 | 4,540 | 11,452 | 584 | 456 | 10,440 |
| `time-divide-and-conquer-connectivity` | 45,498 | 41,744 | 35,538 | 9,960 | 31,784 | 819 | 570 | 30,431 |
| `top-tree-basic` | 110,519 | 101,292 | 78,343 | 32,176 | 69,116 | 1,193 | 798 | 67,173 |
| `treap-map-quick-start` | 29,934 | 27,272 | 22,575 | 7,359 | 19,913 | 737 | 646 | 18,559 |
| `treap-sequence-quick-start` | 28,888 | 26,190 | 21,515 | 7,373 | 18,817 | 597 | 532 | 17,716 |
| `triangulation-quick` | 64,873 | 58,495 | 50,306 | 14,567 | 43,928 | 1,072 | 722 | 42,183 |
| `trie-quick` | 12,510 | 11,416 | 10,026 | 2,484 | 8,932 | 448 | 418 | 8,087 |
| `types-and-utilities-composite-key` | 11,364 | 10,503 | 9,362 | 2,002 | 8,501 | 145 | 266 | 8,095 |
| `virtual-tree-quick` | 23,547 | 21,296 | 18,512 | 5,035 | 16,261 | 1,220 | 646 | 14,451 |
| `virtual-tree-separation` | 24,159 | 21,848 | 18,909 | 5,250 | 16,598 | 1,254 | 646 | 14,756 |
| `wavelet-matrix-basic` | 10,544 | 9,564 | 8,842 | 1,702 | 7,862 | 219 | 380 | 7,272 |
| `weighted-dsu-matrix` | 32,471 | 29,346 | 26,438 | 6,033 | 23,313 | 459 | 684 | 22,185 |
| `weighted-dsu-potentials` | 38,097 | 34,570 | 30,394 | 7,703 | 26,867 | 692 | 760 | 25,441 |
| `xor-basis-queries` | 7,372 | 6,662 | 5,959 | 1,413 | 5,249 | 106 | 342 | 4,804 |
| **Total** | **3,811,229** | **3,452,529** | **2,937,419** | **873,810** | **2,578,719** | **87,024** | **68,666** | **2,426,901** |
