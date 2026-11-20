# SPDX-License-Identifier: GPL-3.0-or-later
"""Real-binary CLI surface checks; run only in the SDK job."""
from pathlib import Path
import os
import shutil
from contextlib import contextmanager
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
passed = 0
failed = 0
checks = 0
active = ''


def check(condition, details):
    global failed, checks
    checks += 1
    if not condition:
        failed += 1
        print(f'FAIL {active}: {details!r}', file=sys.stderr)


@contextmanager
def case(name):
    global active, passed
    active = name
    before = failed
    try:
        yield
    except Exception as error:
        check(False, str(error))
    if before == failed:
        passed += 1
        print(f'PASS {name}')

with tempfile.TemporaryDirectory(prefix='.distill-cli-options-', dir=Path.cwd()) as work:
    root = Path(work)
    (root / 'main.cpp').write_text('// SPDX-License-Identifier: GPL-3.0-or-later\n#if 1\nint main(){}\n#endif\n')
    for args, code, message in [
        (['--help'], 0, 'Usage: distill'),
        (['--no-assume=A11', 'main.cpp'], 1, 'switchable names: instantiation-effects'),
        (['--no-assume=unknown', 'main.cpp'], 1, 'switchable names: instantiation-effects'),
        (['--no-assume=', 'main.cpp'], 1, 'switchable names: instantiation-effects'),
        (['--no-assume=instantiation-effects,', 'main.cpp'], 1, 'switchable names: instantiation-effects'),
        (['g++', '-std=gnu++20', 'main.cpp'], 0, 'distill: wrote pruned (local)'),
        (['--no-prune'], 1, 'missing compile command'),
        (['--no-prune', '-o'], 1, 'missing value'),
        (['--no-prune', '--unknown'], 1, 'unknown distill option'),
        (['--keep', 'unknown_function', 'g++', '-std=gnu++20', 'main.cpp'], 1, '--keep unknown_function: no indexed declaration'),
        (['--keep'], 1, 'missing value for --keep'),
        (['--verify-run'], 1, 'missing value for --verify-run'),
        (['--verify-run', 'missing.in', 'main.cpp'], 1, 'not a sample file or directory'),
        (['--no-prune', '--strip-comments', 'main.cpp'], 0, 'distill: wrote flat (local)'),
        (['--no-prune', 'g++', '-c', 'main.cpp'], 1, 'rejected option'),
        (['--no-prune', '-o', 'main.cpp', 'main.cpp'], 1, 'must differ'),
        (['--no-prune', 'main.cpp'], 0, 'distill: wrote flat (local), runtime not requested'),
        (['--no-prune', '--strict', 'main.cpp'], 3, 'distill: wrote flat (local), runtime not requested'),
    ]:
        with case(repr(args)):
            result = subprocess.run([binary, *args], cwd=root, capture_output=True, text=True, timeout=120)
            check(result.returncode == code, (args, result.returncode, result.stderr))
            check(message in result.stdout + result.stderr, (args, result.stdout, result.stderr))
            check(set(root.iterdir()) == {root / 'main.cpp'}, list(root.iterdir()))
    with case('instantiation-effects flag reaches the worker and modes do not mix'):
        saved = (root / 'main.cpp').read_bytes()
        (root / 'main.cpp').write_text('template<class T> struct Effect { friend int supplied(T); };\n'
                                      'void dormant(){(void)sizeof(Effect<int>);}\nint main(){}\n')
        selections = [[], ['--no-assume=instantiation-effects'],
                      ['--no-assume=instantiation-effects,instantiation-effects', '--no-assume=instantiation-effects'], []]
        for flags in selections:
            result = subprocess.run([binary, *flags, '--report', 'report.txt', 'g++', '-std=gnu++20', 'main.cpp'],
                                    cwd=root, capture_output=True, text=True, timeout=120)
            check(result.returncode == 0 and 'wrote pruned (local)' in result.stderr, result.stderr)
            report = (root / 'report.txt').read_text()
            check(('void dormant' in result.stdout) == bool(flags), result.stdout)
            check(('pruning assumption: A11 instantiation-effects\n' in report) == (not flags), report)
            check(('policy: D3d-7, without instantiation-effects\n' in report) == bool(flags), report)
            if flags:
                check('pruning assumption disabled: A11 instantiation-effects (--no-assume=instantiation-effects)' in report, report)
        (root / 'main.cpp').write_bytes(saved)
        (root / 'report.txt').unlink()
    with case('judge list, full names, aliases and collision diagnostics'):
        config = root / 'profile-config'
        judges = config / 'distill' / 'judges'
        judges.mkdir(parents=True)
        env = dict(os.environ, XDG_CONFIG_HOME=str(config), APPDATA=str(config))

        def judge_run(selector, compile=False):
            return subprocess.run([binary, '--judge', selector, *(['g++', '-c', 'main.cpp'] if compile else [])],
                                  env=env, cwd=root, capture_output=True, text=True, timeout=120)

        result = judge_run('list')
        check(result.returncode == 0 and result.stderr == '', result.stderr)
        rows = [line.split('\t') for line in result.stdout.splitlines()]
        expected = [
            ['atcoder-gcc15-gnupp23', 'atcoder', 'bundled'],
            ['codeforces-gcc14-cpp23', 'codeforces', 'bundled']]
        expected.append(['cses-gcc13-cpp23', 'cses', 'bundled'])
        expected.append(['kattis-gcc15-gnupp23', 'kattis', 'bundled'])
        expected.append(['library-checker-gcc15-cpp23', 'library-checker', 'bundled'])
        expected.append(['qoj-gcc15-cpp23', 'qoj', 'bundled'])
        expected.append(['timus-gcc13-cpp23', 'timus', 'bundled'])
        expected.append(['yukicoder-gcc16-cpp23', 'yukicoder', 'bundled'])
        expected.append(['yukicoder-gcc16-gnupp23', '-', 'bundled'])
        check(len(rows) == len(expected) and all(len(row) == 7 and row[6] == '-' for row in rows), rows)
        check([row[:3] for row in rows] == expected, rows)
        for name, alias, _ in expected:
            for selector in ((alias, name) if alias != '-' else (name,)):
                result = judge_run(selector, compile=True)
                check(result.returncode == 1 and 'rejected option' in result.stderr, result.stderr)
                check(f'judge: {selector} -> {name}\n' in result.stderr, result.stderr)
        profile = ("name = 'custom-gcc14-cpp23'\nalias = 'codeforces'\ndescription = 'CLI fixture'\n"
                   "triple = 'x86_64-w64-mingw32'\ncompiler = { family = 'gcc', version = '14.2.0' }\n"
                   "std = 'c++23'\nflags = []\nlibraries = ['mingw-w64']\ncheck = []\n"
                   "updated = '2026-11-16'\nsources = ['https://example.org/fixture']\n")
        (judges / 'custom-gcc14-cpp23.toml').write_text(profile, encoding='utf-8')
        for run in range(2):
            result = judge_run('codeforces', compile=True)
            check("shadows bundled identifier 'codeforces'" in result.stderr, result.stderr)
            check('judge: codeforces -> custom-gcc14-cpp23\n' in result.stderr, result.stderr)
        for alias in ('codeforces', 'custom-gcc14-cpp23'):
            other = profile.replace("name = 'custom-gcc14-cpp23'", "name = 'other-gcc14-cpp23'")
            other = other.replace("alias = 'codeforces'", f"alias = '{alias}'")
            (judges / 'other-gcc14-cpp23.toml').write_text(other, encoding='utf-8')
            result = judge_run('list')
            check(result.returncode == 1 and 'identifier collision' in result.stderr, result.stderr)
            check('custom-gcc14-cpp23.toml' in result.stderr and 'other-gcc14-cpp23.toml' in result.stderr, result.stderr)
        shutil.rmtree(config)
    with case('atomic output replacement and failure preserves previous output'):
        output = root / 'submit.cpp'
        output.write_bytes(b'old checked artifact')
        result = subprocess.run([binary, '-o', str(output), 'g++', '-std=gnu++20', 'main.cpp'],
                                cwd=root, capture_output=True, timeout=120)
        check(result.returncode == 0, result.stderr)
        check(b'int main(){}' in output.read_bytes(), output.read_bytes())
        saved = output.read_bytes()
        result = subprocess.run([binary, '-o', str(output), '--keep', 'unknown_function',
                                 'g++', '-std=gnu++20', 'main.cpp'], cwd=root, capture_output=True, timeout=120)
        check(result.returncode == 1, result.stderr)
        check(result.stdout == b'', result.stdout)
        check(output.read_bytes() == saved, output.read_bytes())
        output.unlink()
        check(set(root.iterdir()) == {root / 'main.cpp'}, list(root.iterdir()))
    with case('verify-run file, sorted directory, repetition, verbose report'):
        saved_source = (root / 'main.cpp').read_bytes()
        (root / 'main.cpp').write_text('#include <cstdio>\nint main(){int x; if(std::scanf("%d", &x)!=1) return 2; std::printf("%d\\n",x*2); return x<0?7:0;}\n')
        (root / 'single.txt').write_text('3')
        samples = root / 'samples'; samples.mkdir()
        (samples / 'z.in').write_text('-2')
        (samples / 'a.in').write_text('5')
        (samples / 'ignored.txt').write_text('not input')
        (samples / 'nested').mkdir()
        (samples / 'nested' / 'ignored.in').write_text('not input')
        for selection, count in [(['--verify-run', 'single.txt'], 1),
                                 (['--verify-run', 'samples'], 2),
                                 (['--verify-run', 'single.txt', '--verify-run', 'samples', '--verify-run', 'single.txt'], 4)]:
            result = subprocess.run([binary, '-v', '--report', 'report.txt', *selection,
                                     'g++', '-std=gnu++20', 'main.cpp'], cwd=root,
                                    capture_output=True, text=True, timeout=120)
            check(result.returncode == 0, result.stderr)
            check(f'runtime compared {count} samples' in result.stderr, result.stderr)
            check(f'runtime compared {count} samples' in (root / 'report.txt').read_text(), result.stderr)
            check(result.stderr.count('raw checks: sample ') == count, result.stderr)
            if 'samples' in selection:
                a, z = str(Path('samples') / 'a.in'), str(Path('samples') / 'z.in')
                check(result.stderr.index('sample ' + a) < result.stderr.index('sample ' + z), result.stderr)
        empty = root / 'empty'; empty.mkdir()
        for name in ['empty', 'samples/nested/missing.in']:
            result = subprocess.run([binary, '--verify-run', name, '-o', 'submit.cpp', 'main.cpp'],
                                    cwd=root, capture_output=True, text=True, timeout=120)
            check(result.returncode == 1 and result.stdout == '', result.stderr)
            check('--verify-run' in result.stderr, result.stderr)
            check(not (root / 'submit.cpp').exists(), result.stderr)
        (root / 'main.cpp').write_bytes(saved_source)
        (root / 'report.txt').unlink(); (root / 'single.txt').unlink()
        shutil.rmtree(samples); empty.rmdir()
    with case('verify-run rejects nondeterministic raw and keeps work'):
        saved_source = (root / 'main.cpp').read_bytes()
        (root / 'main.cpp').write_text('#include <cstdio>\nint main(){int x=0; std::printf("%p\\n",static_cast<void*>(&x));}\n')
        (root / 'sample.in').write_bytes(b'')
        result = subprocess.run([binary, '--verify-run', 'sample.in', '--report', 'report.txt',
                                 '-o', 'submit.cpp', 'g++', '-std=gnu++20', 'main.cpp'],
                                cwd=root, capture_output=True, text=True, timeout=120)
        check(result.returncode == 1 and result.stdout == '', result.stderr)
        check('raw failed: differs on sample sample.in (distill bug or nondeterministic program)' in result.stderr, result.stderr)
        check(not (root / 'submit.cpp').exists(), result.stderr)
        marker = 'work files kept at '
        if marker in result.stderr:
            kept = Path(result.stderr.split(marker, 1)[1].splitlines()[0])
            check((kept / 'raw.cpp').exists(), result.stderr)
            shutil.rmtree(kept)
        else:
            check(False, result.stderr)
        check('differs on sample sample.in' in (root / 'report.txt').read_text(), result.stderr)
        (root / 'main.cpp').write_bytes(saved_source)
        (root / 'sample.in').unlink(); (root / 'report.txt').unlink()
    # Do not use text=True here: universal newline conversion would hide a
    # Windows text-mode write in stdout, the published artifact or the report.
    source = (root / 'main.cpp').read_bytes()
    (root / 'main.cpp').write_bytes(b'// SPDX-License-Identifier: GPL-3.0-or-later\r\n#if 1\r\nint main(){}\r\n#endif\r\n')
    outputs = []
    for output_args in ([], ['-o', 'submit.cpp']):
        with case(f'line endings {output_args}'):
            result = subprocess.run([binary, '--no-prune', '--report', 'report.txt', *output_args, 'main.cpp'],
                                    cwd=root, capture_output=True, timeout=120)
            check(result.returncode == 0, result.stderr)
            check(b'distill: wrote flat (local)' in result.stderr, result.stderr)
            output = (root / 'submit.cpp').read_bytes() if output_args else result.stdout
            check(b'int main(){}\r\n' in output, output)
            check(b'#if' not in output and b'\r\r\n' not in output, output)
            check(b'\r' not in (root / 'report.txt').read_bytes(), 'report contains CR bytes')
            check(b'\r' not in result.stderr, result.stderr)
            if output_args:
                check(result.stdout == b'', result.stdout)
                (root / 'submit.cpp').unlink()
            (root / 'report.txt').unlink()
            outputs.append(output)
    with case('matching output bytes'):
        check(len(outputs) == 2 and outputs[0] == outputs[1], outputs)
    (root / 'main.cpp').write_bytes(source)
    if os.name != 'nt':
        farm = root / 'symlink-farm'
        farm.mkdir()
        alias = farm / 'distill'
        alias.symlink_to(binary)
        for executable, argv0 in [(binary, 'distill-custom-argv0-not-on-path-17931'),
                                  (str(alias), str(alias))]:
            with case(f'executable {argv0}'):
                result = subprocess.run([argv0, '--no-prune', 'main.cpp'], executable=executable,
                                        cwd=root, capture_output=True, text=True, timeout=120)
                check(result.returncode == 0, (argv0, result.stderr))
                check('distill: wrote flat (local)' in result.stderr, result.stderr)
                check('#if' not in result.stdout, result.stdout)
                check(set(root.iterdir()) == {root / 'main.cpp', farm}, list(root.iterdir()))
print(f'{passed} CLI cases passed; {checks} checks, {failed} failures')
sys.exit(1 if failed else 0)
