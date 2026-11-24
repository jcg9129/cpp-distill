# SPDX-License-Identifier: GPL-3.0-or-later
"""Local GCC coverage and original/compact stdout comparison (fixed examples).

Coverage describes these executions only; absence is not a portable deletion
proof. All generated files and compiler temporary files stay in the worktree.
"""
from common import *
import concurrent.futures
import gzip
import os
import subprocess


def run_one(example):
    dest = OUT / 'runs' / example
    dest.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMPDIR=str(OUT / 'tmp'))
    def command(args):
        result = subprocess.run(args, cwd=dest, env=env, capture_output=True, timeout=180)
        with (dest / 'commands.log').open('ab') as log:
            log.write((' '.join(map(str, args)) + '\n').encode() + result.stdout + result.stderr)
        assert result.returncode == 0, (example, args, result.stderr.decode()[-3000:])
        return result.stdout
    for p in dest.glob('*.gcda'):
        p.unlink()
    command(['g++', '-std=c++23', '-O0', '-fno-inline', '--coverage',
             str(folder(example) / 'distill-compact.cpp'), '-o', str(dest / 'compact')])
    compact = command([str(dest / 'compact')])
    command(['g++', '-std=c++23', '-O0', '-I' + str(ATL / 'include'),
             str(example_path(example)), '-o', str(dest / 'original')])
    original = command([str(dest / 'original')])
    assert compact == original, (example, compact, original)
    (dest / 'stdout.txt').write_bytes(compact)
    notes = list(dest.glob('*.gcno'))
    assert len(notes) == 1
    command(['gcov', '-j', '-b', '-c', str(notes[0])])
    data = json.loads(gzip.decompress(next(dest.glob('*.gcov.json.gz')).read_bytes()))
    target = [f for f in data['files'] if f['file'] == str(folder(example) / 'distill-compact.cpp')]
    assert len(target) == 1
    return dict(example=example, stdout=compact.decode(), compiler=data['gcc_version'],
                functions=target[0]['functions'], lines=target[0]['lines'])


def main():
    (OUT / 'tmp').mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results = []
        for result in pool.map(run_one, NAMES):
            results.append(result)
            print('PASS original == compact:', result['example'], flush=True)
    dump('coverage.json', results)


if __name__ == '__main__':
    main()
