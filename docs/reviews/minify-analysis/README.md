# Minify analysis scripts

Run from the repository root. Dependencies in the measurement environment:
Python 3.14 (standard library only), `dpkg-deb`, GCC 15.2, libclang 22.1.8,
and the native AArch64 Linux system headers. No packages are installed globally.
The downloaded libstdc++ 13.3 headers, transformed sources, declaration caches,
complete rename maps and compiler logs live in ignored `measurements/minify-analysis/`.

```sh
python3 docs/reviews/minify-analysis/prepare_headers.py
python3 -m unittest discover -s docs/reviews/minify-analysis -p 'test_*.py' -v
python3 docs/reviews/minify-analysis/measure.py
python3 docs/reviews/minify-analysis/verify_tokens.py
python3 docs/reviews/minify-analysis/verify.py --jobs 3
python3 docs/reviews/minify-analysis/evidence.py
python3 docs/reviews/minify-analysis/write_report.py
```

`measure.py --only NAME` runs one example and writes suffixed result files.
The default input is the read-only corpus specified in the review. Its byte
sizes must match `summary.tsv`. `clang_inventory.py` uses ctypes to visit user
declarations, record the include graph, and independently raw-tokenize files.
`measure.py` assigns short names simultaneously, so eligible old spellings may
be reused. It breaks equal-frequency ties alphabetically. `renamed_spellings`
counts assignments, including any assignment of a spelling to itself;
`evidence.json` distinguishes assignments that actually change spelling.

`results.tsv` contains all requested per-example byte columns. `results.json`
also contains totals, strict over-limit counts and the still-too-big list.
The manifests fingerprint every input `.cpp`, its `.rep`, and every inventoried
header. `verification.json` records compiler checks and hashes of the exact
transformed files checked; `token-verification.json` records Clang lexer checks.
`evidence.json` records protocol and source-observation examples for the review.

This is a corpus measurement, not a general C++ source transformer. The custom
scanner covers the ASCII preprocessing tokens in these inputs and its token
sequences are checked independently with Clang. The native GCC 13 package paths
and installed Clang resource directory are explicit in `measure.py` / `verify.py`.
Other hosts require adjusting those paths and rebuilding declaration caches.
