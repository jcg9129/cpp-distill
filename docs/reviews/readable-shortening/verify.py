#!/usr/bin/env python3
"""Compile transformed outputs and independently compare Clang raw tokens."""
import argparse
from bisect import bisect_right
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from abbreviate import baseline
from measure_readable import WORK, OUT, COLUMNS, compiler_flags
from clang_inventory import raw_tokens


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--tokens-only', action='store_true')
    parser.add_argument('--compile-only', action='store_true')
    args = parser.parse_args()
    audit = json.loads((WORK / 'renames.json').read_text())
    variants = COLUMNS[1:]
    logdir = WORK / 'compile-logs'
    logdir.mkdir(exist_ok=True)
    tmp = (WORK / 'tmp').resolve()
    tmp.mkdir(exist_ok=True)
    env = dict(os.environ, TMPDIR=str(tmp))
    if not args.tokens_only:
        def compile_one(task):
            name, variant = task
            path = WORK / 'outputs' / name / (variant + '.cpp')
            start = time.monotonic()
            done = subprocess.run(['g++', '-fsyntax-only'] + compiler_flags() + [str(path)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120, env=env)
            (logdir / (name + '.' + variant + '.log')).write_bytes(done.stdout)
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            assert digest == audit[name]['output_sha256'][variant]
            return dict(example=name, variant=variant, exit=done.returncode, sha256=digest,
                        seconds=round(time.monotonic() - start, 3))
        tasks = [(name, v) for name in sorted(audit) for v in variants]
        rows = []
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for i, row in enumerate(pool.map(compile_one, tasks), 1):
                rows.append(row)
                print(f'{i}/{len(tasks)} {row["example"]} {row["variant"]}: {row["exit"]}', flush=True)
                (WORK / 'compile-partial.json').write_text(json.dumps(rows, indent=2) + '\n')
        result = dict(checks=len(rows), passed=sum(r['exit'] == 0 for r in rows),
                      failed=[r for r in rows if r['exit']], rows=rows)
        (OUT / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
        assert not result['failed'], result['failed']
    if not args.compile_only:
        rows = []
        for i, (name, info) in enumerate(sorted(audit.items()), 1):
            source = Path(info['source_path'])
            data = source.read_bytes()
            assert hashlib.sha256(data).hexdigest() == info['source_sha256']
            before = raw_tokens(source.resolve(), offsets=True)
            identifiers = {t.start for t in baseline.lex(data.decode('latin1')) if t.kind == 'id'}
            mapping = {k: v['new'] for k, v in info['renames'].items()}
            renamed = [(k, mapping.get(s, s) if offset in identifiers else s) for k, s, offset in before]
            clean, _, _ = baseline.cleanup(data.decode('latin1'), baseline.lex(data.decode('latin1')))
            clean_path = WORK / 'outputs' / name / 'compact_pr129.cpp'
            # The independent lexer reads the actual cleanup output; also verify
            # exact original-line removal against the reused baseline function.
            assert clean_path.read_bytes() == clean.encode('latin1')
            # Match cleanup as a subsequence of original physical lines. Filter
            # the *original* Clang stream, rather than comparing cleanup to its
            # own tokenization (which would make that check tautological).
            original_lines = data.decode('latin1').splitlines(keepends=True)
            clean_lines = clean.splitlines(keepends=True)
            offsets, kept_lines, end, next_line = [], [], 0, 0
            for line in original_lines:
                offsets.append(end)
                end += len(line)
                keep = next_line < len(clean_lines) and line == clean_lines[next_line]
                kept_lines.append(keep)
                next_line += keep
            assert next_line == len(clean_lines), (name, 'cleanup is not whole-line removal')
            clean_original = [(k, s, offset) for k, s, offset in before
                              if kept_lines[bisect_right(offsets, offset) - 1]]
            clean_before = [(k, s) for k, s, _ in clean_original]
            clean_renamed = [(k, mapping.get(s, s) if offset in identifiers else s) for k, s, offset in clean_original]
            checks = {}
            for variant in variants:
                target = (clean_renamed if variant.startswith('r') else clean_before) if 'pr129' in variant else renamed
                path = WORK / 'outputs' / name / (variant + '.cpp')
                assert hashlib.sha256(path.read_bytes()).hexdigest() == info['output_sha256'][variant]
                actual = raw_tokens(path.resolve())
                checks[variant] = actual == target
                if actual != target:
                    first = next((j for j, (a, b) in enumerate(zip(actual, target)) if a != b), min(len(actual), len(target)))
                    raise AssertionError((name, variant, first, actual[first:first + 3], target[first:first + 3]))
            rows.append(dict(example=name, checks=checks))
            print(f'{i}/{len(audit)} {name}: Clang tokens match', flush=True)
        result = dict(examples=len(rows), checks=sum(len(r['checks']) for r in rows),
                      passed=sum(sum(r['checks'].values()) for r in rows), rows=rows)
        (OUT / 'token-verification.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
