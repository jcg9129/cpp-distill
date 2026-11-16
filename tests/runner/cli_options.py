# SPDX-License-Identifier: GPL-3.0-or-later
"""Real-binary CLI surface checks; run only in the SDK job."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
passed = 0
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
        result = subprocess.run([binary, *args], cwd=root, capture_output=True, text=True, timeout=120)
        assert result.returncode == code, (args, result.returncode, result.stderr)
        assert message in result.stdout + result.stderr, (args, result.stdout, result.stderr)
        assert set(root.iterdir()) == {root / 'main.cpp'}, list(root.iterdir())
        passed += 1
    if os.name != 'nt':
        farm = root / 'symlink-farm'
        farm.mkdir()
        alias = farm / 'distill'
        alias.symlink_to(binary)
        for executable, argv0 in [(binary, 'distill-custom-argv0-not-on-path-17931'),
                                  (str(alias), str(alias))]:
            result = subprocess.run([argv0, '--no-prune', 'main.cpp'], executable=executable,
                                    cwd=root, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, (argv0, result.stderr)
            assert 'distill: wrote flat (local)' in result.stderr, result.stderr
            assert '#if' not in result.stdout, result.stdout
            assert set(root.iterdir()) == {root / 'main.cpp', farm}, list(root.iterdir())
            passed += 1
print(f'{passed} CLI surface checks passed')
