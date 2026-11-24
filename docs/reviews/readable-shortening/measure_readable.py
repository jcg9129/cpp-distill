#!/usr/bin/env python3
"""Measure R using the earlier analysis' exact inputs, exclusions and cleanup."""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import time

from abbreviate import baseline, mapping_for, collapse_spaces, PROTOCOLS
from clang_inventory import inventory, raw_tokens

WORK = Path('measurements/readable-shortening')
OUT = Path('docs/reviews/readable-shortening')
PRIOR = Path('docs/reviews/minify-analysis')
INPUT = Path('/home/appuser/.cache/caide-gap/caide2/artifacts')
COLUMNS = ['compact', 'r', 'r_spaces', 'compact_pr129', 'r_pr129', 'r_spaces_pr129']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def compiler_flags(work=WORK):
    gcc = (work / 'gcc13/usr/include').resolve()
    return ['-std=c++23', '-nostdinc++', '-isystem', str(gcc / 'c++/13'),
            '-isystem', str(gcc / 'aarch64-linux-gnu/c++/13'),
            '-isystem', str(gcc / 'c++/13/backward')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only', default='')
    args = parser.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    flags = ['-x', 'c++'] + compiler_flags() + ['-resource-dir', '/usr/lib/llvm-22/lib/clang/22']
    adl = {l.split()[1] for l in Path('data/names/adl.txt').read_text().splitlines()
           if l and not l.startswith('#')}
    macro_text = subprocess.check_output(['g++', '-std=c++23', '-dM', '-E', '-x', 'c++', '-'], input=b'').decode()
    predefined = {line.split()[1].split('(')[0] for line in macro_text.splitlines()}
    (OUT / 'predefined-macros.txt').write_text('\n'.join(sorted(predefined)) + '\n')
    expected_hashes = {r['example']: r['cpp_sha256'] for r in csv.DictReader(
        (PRIOR / 'input-manifest.tsv').open(), delimiter='\t')}
    prior_rows = {r['example']: r for r in json.loads((PRIOR / 'results.json').read_text())['rows']}
    expected_headers = {r['path']: r['sha256'] for r in csv.DictReader(
        (PRIOR / 'header-manifest.tsv').open(), delimiter='\t')}
    sources = sorted(INPUT.glob('caide-compare-shard-*/examples/*/distill-compact.cpp'), key=lambda p: p.parent.name)
    assert len(sources) == len({p.parent.name for p in sources}) == 126
    assert {p.parent.name for p in sources} == expected_hashes.keys()
    header_cache_path = WORK / 'header-identifiers.json'
    header_cache = json.loads(header_cache_path.read_text()) if header_cache_path.exists() else {}
    rows, audits, all_headers, samples = [], {}, {}, Counter()
    for number, path in enumerate(sources, 1):
        name = path.parent.name
        if args.only and name != args.only:
            continue
        start = time.monotonic()
        data = path.read_bytes()
        assert sha(data) == expected_hashes[name], name + ': baseline input changed'
        source = data.decode('latin1')
        assert len(source) == prior_rows[name]['compact']
        tokens = baseline.lex(source)
        cache = WORK / (name + '.inventory.json')
        key = sha((source + '\n' + '\n'.join(flags)).encode('latin1'))
        if cache.exists() and (inv := json.loads(cache.read_text())).get('key') == key:
            pass
        else:
            inv = inventory(path.resolve(), flags)
            inv['key'] = key
            cache.write_text(json.dumps(inv, indent=2) + '\n')
        library = set()
        for header in inv['headers']:
            hpath = Path(header)
            digest = sha(hpath.read_bytes())
            normalized = header.replace(str((WORK / 'gcc13').resolve()), '$GCC13')
            assert expected_headers.get(normalized) == digest, ('header changed', normalized)
            all_headers[normalized] = digest
            if header not in header_cache or header_cache[header]['sha256'] != digest:
                header_cache[header] = dict(sha256=digest, names=sorted({s for k, s in raw_tokens(hpath) if k == 2}))
            library.update(header_cache[header]['names'])
        header_cache_path.write_text(json.dumps(header_cache, sort_keys=True) + '\n')
        assert len(library) == prior_rows[name]['library_identifiers']
        assert len(inv['headers']) == prior_rows[name]['headers']
        kept, _, _ = baseline.exclusions(tokens, source, library, adl)
        mapping, counts, collisions = mapping_for(tokens, inv['declared'], kept, predefined)
        # Confirm that *exactly* the old T2 universe is reproduced, before R's
        # extra restrictions. This is independent of the new abbreviation map.
        old_map, _ = baseline.mapping_for(tokens, inv['declared'], kept)
        assert len(old_map) == prior_rows[name]['renamed_spellings']
        assert sum(counts[k] * (len(k) - len(v)) for k, v in old_map.items()) == len(source) - prior_rows[name]['t2']
        clean, inc, once = baseline.cleanup(source, tokens)
        assert (inc, once) == (prior_rows[name]['duplicate_include_bytes'], prior_rows[name]['unused_once_bytes'])
        renamed = baseline.render(source, tokens, mapping)
        renamed_clean = baseline.render(clean, baseline.lex(clean), mapping)
        outputs = dict(compact=source, r=renamed, r_spaces=collapse_spaces(renamed),
                       compact_pr129=clean, r_pr129=renamed_clean, r_spaces_pr129=collapse_spaces(renamed_clean))
        assert len(source) - len(renamed) == sum(counts[k] * (len(k) - len(v)) for k, v in mapping.items())
        assert len(source) - len(clean) == inc + once
        dest = WORK / 'outputs' / name
        dest.mkdir(parents=True, exist_ok=True)
        for variant, text in outputs.items():
            if variant != 'compact':
                (dest / (variant + '.cpp')).write_bytes(text.encode('latin1'))
            base_tokens = baseline.lex(clean) if 'pr129' in variant else tokens
            use_mapping = mapping if variant.startswith('r') else {}
            expected = [(t.kind, use_mapping.get(t.text, t.text) if t.kind == 'id' else t.text, t.directive) for t in base_tokens]
            assert baseline.signature(baseline.lex(text)) == expected, (name, variant)
            assert [t.text for t in baseline.lex(text) if t.kind == 'literal'] == [t.text for t in base_tokens if t.kind == 'literal']
            original_lines = clean if 'pr129' in variant else source
            assert text.count('\n') == original_lines.count('\n'), (name, variant, 'line count')
        row = dict(example=name, **{c: len(outputs[c]) for c in COLUMNS},
                   duplicate_include_bytes=inc, unused_once_bytes=once, renamed_spellings=len(mapping),
                   collision_spellings=len(collisions), library_identifiers=len(library), headers=len(inv['headers']))
        rows.append(row)
        audits[name] = dict(source_path=str(path), source_sha256=sha(data),
                           renames={k: dict(new=v, occurrences=counts[k], saved=counts[k] * (len(k) - len(v))) for k, v in mapping.items()},
                           collisions=collisions, protocol_occurrences={p: counts[p] for p in sorted(PROTOCOLS) if counts[p]},
                           old_t2_assignments=len(old_map), output_sha256={v: sha(t.encode('latin1')) for v, t in outputs.items()})
        for old, new in mapping.items():
            samples[old, new] += counts[old]
        print(f'{number}/126 {name}: {len(source)} -> {len(renamed)} -> {len(outputs["r_spaces_pr129"])} ({time.monotonic() - start:.2f}s)', flush=True)
    if not args.only:
        assert all_headers == expected_headers, 'transitive header union differs from earlier analysis'
    result = dict(examples=len(rows), totals={c: sum(r[c] for r in rows) for c in COLUMNS},
                  over={str(cap): {c: sum(r[c] > cap for r in rows) for c in COLUMNS} for cap in [65536, 50000, 32768]},
                  still_over_64k=[{k: r[k] for k in ['example'] + COLUMNS} for r in rows if r['r'] > 65536],
                  duplicate_include_bytes=sum(r['duplicate_include_bytes'] for r in rows),
                  unused_once_bytes=sum(r['unused_once_bytes'] for r in rows),
                  sample_renames=[dict(old=k[0], new=k[1], occurrences=n) for k, n in sorted(samples.items(), key=lambda x: (-x[1], x[0]))[:15]],
                  renamed_spellings=sum(r['renamed_spellings'] for r in rows),
                  collision_spellings=sum(r['collision_spellings'] for r in rows),
                  headers=len(all_headers), rows=rows)
    suffix = '.' + args.only if args.only else ''
    (OUT / ('results' + suffix + '.json')).write_text(json.dumps(result, indent=2) + '\n')
    (WORK / ('renames' + suffix + '.json')).write_text(json.dumps(audits, indent=2) + '\n')
    fields = list(rows[0])
    with (OUT / ('results' + suffix + '.tsv')).open('w') as f:
        writer = csv.DictWriter(f, fields, delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    (OUT / ('input-manifest' + suffix + '.tsv')).write_text('example\tcpp_sha256\n' + ''.join(
        name + '\t' + a['source_sha256'] + '\n' for name, a in audits.items()))
    print(json.dumps({k: v for k, v in result.items() if k != 'rows'}, indent=2))


if __name__ == '__main__':
    main()
