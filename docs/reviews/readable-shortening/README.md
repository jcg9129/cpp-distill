# Readable shortening analysis

Run from the repository root on the same native AArch64 environment as the
[earlier analysis](../minify-analysis/README.md): Python 3.14 (standard library
only), libclang/Clang 22.1.8, GCC 15.2, `dpkg-deb`, host libc headers and the
SHA-256-pinned libstdc++ 13.3.0-16 package. Nothing is installed globally.
All generated sources, caches, downloaded headers and compiler logs stay in
ignored `measurements/readable-shortening/`. Inputs are read-only.

```sh
mkdir -p measurements/readable-shortening/tmp
export TMPDIR="$PWD/measurements/readable-shortening/tmp"
python3 docs/reviews/readable-shortening/prepare_headers.py
python3 -m unittest discover -s docs/reviews/minify-analysis -p 'test_*.py' -v
python3 -m unittest discover -s docs/reviews/readable-shortening -p 'test_*.py' -v
python3 docs/reviews/readable-shortening/measure_readable.py
python3 docs/reviews/readable-shortening/verify.py --jobs 3
python3 docs/reviews/readable-shortening/write_report.py
```

`measure_readable.py --only NAME` is a smoke measurement; it writes suffixed
results and input manifests. A complete run is required before verification
and report generation. `verify.py --tokens-only` and `--compile-only` split
the independent checks when useful. The scripts must retain access to their
sibling `minify-analysis/` directory; its files are imported without changes.

The default input glob is exactly
`/home/appuser/.cache/caide-gap/caide2/artifacts/caide-compare-shard-*/examples/*/distill-compact.cpp`.
No adjacent report, summary, sample-input or other external artifact is read.
The checked-in earlier manifests/results verify all 126 input hashes, byte
sizes, per-example header counts and library-identifier counts, the entire
transitive header hash union, the original T2 assignment counts and byte
savings, and both PR #129 line-byte columns. Host/header drift fails rather
than silently changing the exclusion universe. The compiler's predefined
macro names are recorded in `predefined-macros.txt` and additionally blocked.

`abbreviate.py` specifies the 64-entry dictionary, language-protocol exclusions,
case/digit splitting, collision priority and the restricted space-collapse rule.
Every original source spelling is reserved during allocation, including eligible
ones that will change; a name can restore its own spelling. Frequency priority
is descending identifier-token occurrences within that output, with alphabetical
ties. Expansion restores one additional letter of an original word's prefix,
cycling left-to-right over shortened words until all are restored. Decimal
suffixes starting at `1` are the final fallback. No keyword, reserved spelling,
library name, predefined macro, excluded name or allocated name can be introduced.
Whole identifiers of at most four characters stay unchanged.

`results.tsv` and `results.json` contain all six variants and original-line
cleanup accounting. The JSON also contains strict over-limit counts, the
still-over-64-KiB list and the 15 most frequent **(old, new) pairs** summed across
outputs. One old spelling can expand differently under different per-output
collision sets. Full per-example rename maps, occurrence/saving counts,
collision steps and output hashes are in ignored
`measurements/readable-shortening/renames.json`. Generated `.cpp` files are
under its `outputs/<example>/` subdirectory. `input-manifest.tsv` fingerprints
only the authorized `.cpp` inputs. Header hashes must equal the earlier
[`header-manifest.tsv`](../minify-analysis/header-manifest.tsv).

`verification.json` records syntax-check exit codes and exact output hashes.
`token-verification.json` records independent Clang raw-token comparisons for
all five generated variants. The measurement itself additionally checks exact
literal bytes, directive grouping, line counts, injectivity and byte accounting.
The report generator refuses to report successful verification if the coverage
or any checked output hash differs. This is an analysis, not a production
renamer, runtime-equivalence proof, or judge-policy certification.
