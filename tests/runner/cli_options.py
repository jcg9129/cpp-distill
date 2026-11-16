# SPDX-License-Identifier: GPL-3.0-or-later
"""Real-binary CLI surface checks; run only in the SDK job."""
from pathlib import Path
import os
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
        (['main.cpp'], 1, 'pruning arrives in D3'),
        (['--no-prune'], 1, 'missing compile command'),
        (['--no-prune', '-o'], 1, 'missing value'),
        (['--no-prune', '--unknown'], 1, 'unknown distill option'),
        (['--no-prune', '--keep', 'x', 'main.cpp'], 1, 'not supported yet'),
        (['--no-prune', '--verify-run', 'sample.in', 'main.cpp'], 1, 'not supported yet'),
        (['--no-prune', '--strip-comments', 'main.cpp'], 1, 'not supported yet'),
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
