#!/usr/bin/env python3
"""Render the review from measurements and confirm verification hashes."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--draft',action='store_true')
    args = p.parse_args()
    directory = Path('docs/reviews/minify-analysis')
    result = json.loads((directory/'results.json').read_text())
    evidence = json.loads((directory/'evidence.json').read_text())
    totals = result['totals']
    columns = ['compact','t1','t2','both','all_rules']
    labels = ['Compact','T1','T2','T1+T2','T1+T2+R']
    def n(value):
        return f'{value:,}'
    def saving(column):
        return n(totals['compact']-totals[column])
    def percent(column):
        return f'{100*(totals["compact"]-totals[column])/totals["compact"]:.2f}%'
    validation = 'Final compiler / independent lexer checks are being refreshed.'
    if not args.draft:
        compiler = json.loads((directory/'verification.json').read_text())
        lexer = json.loads((directory/'token-verification.json').read_text())
        assert compiler['passed']==compiler['checks']==504 and not compiler['failed']
        assert lexer['passed']==lexer['checks']==378
        for row in compiler['rows']:
            path = Path('measurements/minify-analysis/outputs')/row['example']/(row['variant']+'.cpp')
            assert hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256'], str(path)
        validation = ('**504/504 syntax checks pass**: all 126 examples in each of T1, T2, both, and both+R, '
                      'using GCC 15.2 with the downloaded GCC 13 headers and `-std=c++23 -fsyntax-only`. '
                      '**378/378 independent Clang lexer comparisons pass**: original versus T1, and the '
                      'expected identifier substitution versus T2 and both. The checks normalize phase-2 '
                      'line splices outside literal bodies; literal bytes are also checked exactly by the '
                      'measurement scanner. **12/12 focused tests pass**, covering macro adjacency and '
                      'parameters, directives, comments, header operands, literal preservation, digraph / '
                      'ellipsis boundaries, name assignment and cleanup accounting. '
                      'These are syntax and token checks, not runtime equivalence or judge certification. '
                      'No sample-input files are part of the authorized input set.')
    text = f'''# Spelling-only source-size analysis (2026-11-24)

Analysis only; no production changes. Base `origin/main` / `6205039`, branch
`minify-analysis`. The 126 ATL examples are revision `5b14f2d`, judge `cses`,
distill main with D3f, from the specified read-only comparison shards.

T1+T2 saves **{saving('both')} B ({percent('both')})** in aggregate. The count over
65,536 B falls from **{result['over']['65536']['compact']} to {result['over']['65536']['both']}**.
Adding the parallel include/define cleanup estimate saves **{saving('all_rules')} B
({percent('all_rules')})** in aggregate; the same two examples still exceed 64 KiB.
Shared header text is counted once per output occurrence in these totals.

## Totals and source-size limits

“Over” means strictly greater than the stated cap; an output exactly at the cap fits.
R means repeated standard includes plus unused `DISTILL_ONCE_*` defines removed.

| Measure | {' | '.join(labels)} |
|---|{'|'.join(['---:']*len(labels))}|
| Total bytes | {' | '.join(n(totals[c]) for c in columns)} |
| Bytes saved from compact | {' | '.join(n(totals['compact']-totals[c]) for c in columns)} |
| Reduction | {' | '.join(percent(c) for c in columns)} |
'''
    for limit in ['65536','50000','32768']:
        text += f"| Examples over {n(int(limit))} B | "+' | '.join(str(result['over'][limit][c]) for c in columns)+' |\n'
    text += '\nStill over 65,536 B after T1+T2:\n\n| Example | T1+T2 bytes | T1+T2+R bytes |\n|---|---:|---:|\n'
    for r in result['still_over_64k']:
        text += f"| `{r['example']}` | {n(r['both'])} | {n(r['all_rules'])} |\n"
    text += f'''
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
the {evidence['splices']} existing backslash-newline continuations are retained; the
corpus has no splices inside tokens. Directive bodies are compacted while retaining
their logical-line boundary. This is a lexer-minimal counterfactual under these
preservation constraints, not a production compaction change.
If phase-2 splice erasure is counted as whitespace normalization as well, subtract
{evidence['splices']*2} B from the T1, T1+T2 and T1+T2+R totals: respectively
{n(totals['t1']-evidence['splices']*2)}, {n(totals['both']-evidence['splices']*2)} and
{n(totals['all_rules']-evidence['splices']*2)} B. All over-limit counts stay the same.

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
The result assigns {n(evidence['changed_spellings']+evidence['identity_assignments'])}
per-example spellings ({n(evidence['changed_spellings'])} actually change); all names
fit in one or two bytes. T2 saves **{saving('t2')} B**. Per-spelling counts and maps
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
header operands too. The {evidence['headers']} distinct files include host libc and
Clang builtin headers; [header-manifest.tsv](minify-analysis/header-manifest.tsv)
records every hash. This is a native AArch64 approximation, not a capture of CSES's
x86-64 environment or the multi-build A12 inventory. Macro-generated names absent
from raw spelling and alternative target-selected headers remain limitations.

**R:** retain the first identical angle-bracket standard include in textual order;
count each later entire physical line. Remove an empty `DISTILL_ONCE_*` definition
only when its name has no other identifier-token occurrence anywhere in the output.
All {evidence['includes']} includes in these inputs name standard headers;
{evidence['duplicate_includes']} later occurrences account for **{n(result['duplicate_include_bytes'])} B**.
The {evidence['once_defines']} unused once-defines account for **{n(result['unused_once_bytes'])} B**.
These are original-line byte counts. The extra saving after T1+T2 is
**{n(totals['both']-totals['all_rules'])} B**; it is not their raw sum because T1 has
already removed spaces and some directive-separating newlines become unnecessary.
The combined column removes those lines, keeps the original T2 mapping, and reruns
T1; it does not reoptimize name allocation after cleanup. This is a size estimate
of the parallel rules, not a general proof that repeated inclusion has no effects
under intervening macro changes.

## Verification

{validation}

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
'''
    for row in result['rows']:
        values = [row['compact'],row['t1'],row['t2'],row['compact']-row['t2'],row['both'],
                  row['duplicate_include_bytes'],row['unused_once_bytes'],row['all_rules']]
        text += '| `'+row['example']+'` | '+' | '.join(n(v) for v in values)+' |\n'
    values = [totals['compact'],totals['t1'],totals['t2'],totals['compact']-totals['t2'],totals['both'],
              result['duplicate_include_bytes'],result['unused_once_bytes'],totals['all_rules']]
    text += '| **Total** | '+' | '.join('**'+n(v)+'**' for v in values)+' |\n'
    Path('docs/reviews/2026-11-24-minify-analysis.md').write_text(text)


if __name__ == '__main__':
    main()
