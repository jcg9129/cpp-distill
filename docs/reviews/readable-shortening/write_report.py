#!/usr/bin/env python3
"""Render the readable-shortening review from verified measurements."""
import hashlib
import json
from pathlib import Path

from abbreviate import ABBREVIATIONS, PROTOCOLS
from measure_readable import WORK, OUT, COLUMNS

LABELS = ['Compact', 'R', 'R + spaces', 'Compact + PR129', 'R + PR129', 'R + spaces + PR129']


def n(value):
    return f'{value:,}'


def main():
    result = json.loads((OUT / 'results.json').read_text())
    compiler = json.loads((OUT / 'verification.json').read_text())
    lexer = json.loads((OUT / 'token-verification.json').read_text())
    audit = json.loads((WORK / 'renames.json').read_text())
    assert result['examples'] == len(audit) == 126
    tasks = {(name, v) for name in audit for v in COLUMNS[1:]}
    assert compiler['checks'] == compiler['passed'] == len(tasks) == 630 and not compiler['failed']
    assert {(r['example'], r['variant']) for r in compiler['rows']} == tasks
    assert lexer['checks'] == lexer['passed'] == 630
    assert {(r['example'], v) for r in lexer['rows'] for v, ok in r['checks'].items() if ok} == tasks
    for row in compiler['rows']:
        path = WORK / 'outputs' / row['example'] / (row['variant'] + '.cpp')
        assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256'] == audit[row['example']]['output_sha256'][row['variant']]
    totals = result['totals']
    base = totals['compact']
    def saving(c):
        return f"{n(base - totals[c])} B ({100 * (base - totals[c]) / base:.2f}%)"
    best_over = [r for r in result['rows'] if r['r_spaces_pr129'] > 65536]
    text = f'''# Readable identifier-shortening analysis (2026-11-24)

Analysis only; no production changes. Base `origin/main` / `2256cfe`, branch
`abbrev-analysis`. The same 126 ATL compact outputs as the
[earlier spelling analysis](2026-11-24-minify-analysis.md) are measured:
distill main with D3f, no indentation, existing statement line breaks retained.

Readable shortening **R saves {saving('r')}**, versus 873,810 B (22.93%) for
the earlier shortest-name T2. R leaves **{result['over']['65536']['r']}** examples
over 65,536 B; R plus space collapse and the estimated PR #129 cleanup saves
**{saving('r_spaces_pr129')}**, leaving **{len(best_over)}** over that limit.
Readability is illustrated by actual renames below; this does not certify
acceptability under a judge's submission rules.

## Totals and source-size limits

R denotes readable renaming in this report. The earlier report used R for
include/define removal; that operation is called **PR129** here. “Spaces” means
collapse runs of ASCII spaces between tokens on one physical line to one space.
It keeps literal, comment and header-name contents, tabs and every line break.
“Over” is strictly greater than the cap; equality fits. Shared header text is
counted at every occurrence in every output.

| Measure | {' | '.join(LABELS)} |
|---|{'|'.join(['---:'] * len(COLUMNS))}|
| Total bytes | {' | '.join(n(totals[c]) for c in COLUMNS)} |
| Bytes saved from compact | {' | '.join(n(base - totals[c]) for c in COLUMNS)} |
| Reduction | {' | '.join(f'{100 * (base - totals[c]) / base:.2f}%' for c in COLUMNS)} |
'''
    for cap in ['65536', '50000', '32768']:
        text += f"| Examples over {n(int(cap))} B | " + ' | '.join(str(result['over'][cap][c]) for c in COLUMNS) + ' |\n'
    text += f'''
R changes **{n(result['renamed_spellings'])} per-output spellings**. Collision
resolution is exercised for {n(result['collision_spellings'])} eligible spellings
(including ones restored to their original spelling). The additional saving from
collapsing spaces after R is **{n(totals['r'] - totals['r_spaces'])} B**; existing
punctuation spacing is otherwise retained.

## Fifteen most frequent actual renames

Counts are identifier-token occurrences, summed over the 126 outputs for each
**(old, new) pair**, sorted by decreasing frequency and then alphabetically.
A spelling can have different expansions in different outputs because the
collision sets differ. These are measured renames, including common header text,
not hand-picked dictionary demonstrations.

| Original | R | Occurrences |
|---|---|---:|
'''
    for row in result['sample_renames']:
        text += f"| `{row['old']}` | `{row['new']}` | {n(row['occurrences'])} |\n"
    text += f'''
## Still over 64 KiB

The best measured variant, R + spaces + PR129, still leaves **{len(best_over)}**
examples over 65,536 B.

All examples exceeding 65,536 B after R are listed below, so the effect of each
additional operation is visible. Remaining over-limit best-variant sizes are bold.

| Example | R | R + spaces | R + PR129 | R + spaces + PR129 |
|---|---:|---:|---:|---:|
'''
    for row in result['still_over_64k']:
        values = [n(row[c]) for c in ['r', 'r_spaces', 'r_pr129', 'r_spaces_pr129']]
        if row['r_spaces_pr129'] > 65536:
            values[-1] = '**' + values[-1] + '**'
        text += '| `' + row['example'] + '` | ' + ' | '.join(values) + ' |\n'
    text += '''
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
'''
    entries = sorted(ABBREVIATIONS.items())
    for i in range(0, len(entries), 2):
        text += '| ' + ' | '.join('`' + v + '`' for pair in entries[i:i + 2] for v in pair) + ' |\n'
    text += f'''
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
{', '.join('`' + p + '`' for p in sorted(PROTOCOLS))}.
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
before applying the additional R rules. All **{result['headers']}** transitive
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
lines total **{n(result['duplicate_include_bytes'])} B** for repeated includes and
**{n(result['unused_once_bytes'])} B** for unused once-defines, exactly matching the
earlier per-example columns. Their sum is **{n(result['duplicate_include_bytes'] + result['unused_once_bytes'])} B**.

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

| Example | {' | '.join(LABELS)} | Repeated includes | Unused once-defines |
|---|{'|'.join(['---:'] * (len(COLUMNS) + 2))}|
'''
    for row in result['rows']:
        values = [row[c] for c in COLUMNS] + [row['duplicate_include_bytes'], row['unused_once_bytes']]
        text += '| `' + row['example'] + '` | ' + ' | '.join(n(v) for v in values) + ' |\n'
    values = [totals[c] for c in COLUMNS] + [result['duplicate_include_bytes'], result['unused_once_bytes']]
    text += '| **Total** | ' + ' | '.join('**' + n(v) + '**' for v in values) + ' |\n'
    Path('docs/reviews/2026-11-24-readable-shortening.md').write_text(text)


if __name__ == '__main__':
    main()
