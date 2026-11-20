# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded lifetime probes; records observations, not a language guarantee.

Run: timeout 180s python3 tests/probes/instantiation_effects/block_static.py
All temporary files, executables, assembly and JSON stay in this worktree.
"""
from pathlib import Path
from collections import Counter
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / '.checks/block-static'
OUT.mkdir(parents=True, exist_ok=True)
os.environ['TMPDIR'] = str(OUT)
source = Path(__file__).with_suffix('.cpp')
rows = []
for compiler in ('g++-15', 'clang++-22', 'g++-13'):
    if not shutil.which(compiler):
        print(compiler, 'unavailable', flush=True)
        continue
    for standard in (17, 20, 23):
        # 1 supplies the C++20 equivalent of C++23's if !consteval.
        dtors = (0,) if standard == 17 else ((0, 1) if standard == 20 else (0, 2))
        for opt in (0, 2):
            for template in (0, 1):
                for dtor in dtors:
                    for thread in (0, 1):
                        for constinit in ((0,) if standard == 17 else (0, 1)):
                            key = f'{compiler}-cxx{standard}-O{opt}-template{template}-dtor{dtor}-thread{thread}-constinit{constinit}'
                            flags = [compiler, f'-std=c++{standard}', '-pedantic-errors', f'-O{opt}',
                                     f'-DTEMPLATE={template}', f'-DDTOR={dtor}',
                                     f'-DTHREAD={thread}', f'-DCONSTINIT={constinit}']
                            assembly = OUT / (key + '.s')
                            result = subprocess.run([*flags, '-S', str(source), '-o', str(assembly)],
                                                    capture_output=True, text=True, timeout=20)
                            (OUT / (key + '.log')).write_text(result.stdout + result.stderr)
                            assert result.returncode == 0, (key, result.stderr)
                            text = assembly.read_text()
                            outputs = []
                            for reach in (0, 1):
                                binary = OUT / 'program'
                                result = subprocess.run([*flags, f'-DREACH={reach}', str(source), '-o', str(binary)],
                                                        capture_output=True, text=True, timeout=20)
                                assert result.returncode == 0, (key, result.stderr)
                                run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5)
                                assert run.returncode == 0 and not run.stderr, (key, run)
                                outputs.append(run.stdout)
                            row = dict(compiler=compiler, standard=standard, opt=opt, template=template,
                                       dtor=dtor, thread=thread, constinit=constinit,
                                       never=outputs[0], reached=outputs[1],
                                       guard='_ZGV' in text,
                                       atexit='__cxa_atexit' in text,
                                       thread_atexit='__cxa_thread_atexit' in text,
                                       startup='.init_array' in text or '_GLOBAL__sub_I' in text)
                            rows.append(row)
                            assert outputs[0] in ('main\n', 'main\ndestroy\n'), (key, outputs)
                            # Record constexpr destruction folding separately:
                            # Clang may elide even the reached destructor.
                            assert outputs[1] in ('main\n', 'main\ndestroy\n'), (key, outputs)
                            if dtor == 0:
                                assert outputs[1] == 'main\ndestroy\n', (key, outputs)
    print(compiler, Counter((r['standard'], r['never'], r['reached']) for r in rows if r['compiler'] == compiler), flush=True)
(OUT / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
print(f'{len(rows)} configurations, {2 * len(rows)} compile/run comparisons; all controls passed')
