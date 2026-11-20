# SPDX-License-Identifier: GPL-3.0-or-later
"""Run from the worktree: python3 tests/probes/instantiation_effects/run.py.

Compiler probes, not pruning tests. Every process is bounded; outputs stay in
.checks/probes. Deduced-return redeclarations deliberately expose a GCC/Clang
difference; report both outcomes rather than mistaking one for a portable proof.
"""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().parent
OUT = ROOT / '.checks' / 'probes'
OUT.mkdir(parents=True, exist_ok=True)
os.environ['TMPDIR'] = str(OUT)


def probe(compiler, source, flags=(), accepted=True, output=None):
    name = '-'.join([compiler, source, *[f.replace('-D', '') for f in flags]])
    binary = OUT / 'program'
    result = subprocess.run([compiler, '-std=c++20', '-O0', *flags,
                             str(SOURCE / (source + '.cpp')), '-o', str(binary)],
                            capture_output=True, text=True, timeout=20, cwd=ROOT)
    (OUT / (name + '.log')).write_text(result.stdout + result.stderr)
    success = result.returncode == 0
    if accepted is not None:
        assert success == accepted, (name, result.stderr)
    runtime = ''
    if success:
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5, cwd=ROOT)
        assert run.returncode == 0 and not run.stderr, (name, run)
        runtime = run.stdout
        if output is not None:
            assert runtime == output, (name, runtime)
    print(name, 'accepted ' + repr(runtime) if success else 'rejected')


for compiler in ('g++', 'clang++'):
    for form in range(3):
        for deduced in range(2):
            for seed in range(2):
                flags = (f'-DFORM={form}', f'-DDEDUCED={deduced}', f'-DSEED={seed}')
                # Fixed return type is the portable counterexample. GCC 15
                # rejects the auto redeclaration even when the seed survives.
                accepted = bool(seed) if not deduced else (False if not seed else None)
                probe(compiler, 'friend_address', flags, accepted, 'present\n')
    probe(compiler, 'friend_lookup', output='')
    probe(compiler, 'friend_deduction', accepted=None)
    probe(compiler, 'friend_deduction', ('-DSEED',), accepted=None)
    probe(compiler, 'parameter_object', output='main\n')
    probe(compiler, 'parameter_object', ('-DNON_CONSTANT_DESTRUCTION',), accepted=False)
    probe(compiler, 'parameter_object', ('-DBAD_CONSTANT_DESTRUCTION',), accepted=False)
    for selected in range(2):
        for known in ((), ('-DNO_KNOWN',)):
            probe(compiler, 'unseen_argument', (f'-DCLASS_JUDGE={selected}', *known), output='')
