# SPDX-License-Identifier: GPL-3.0-or-later
"""CI CLI smoke using synthetic output; never compiles/runs a probe program."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def smoke(binary, fixtures):
    with tempfile.TemporaryDirectory(prefix='.judge-cli-', dir=Path.cwd()) as temp:
        root = Path(temp)
        profile = root / 'linux-gcc15-gnupp23.toml'
        shutil.copy2(fixtures / profile.name, profile)
        shutil.copy2(fixtures / 'linux-predefined.txt', root)
        with profile.open('a', encoding='utf-8') as out:
            out.write('\nprobe_source_bytes = 16777216\nprobe_output_bytes = 16777216\n')
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'), HOME=str(root), APPDATA=str(root))

        def invoke(*args, code=0):
            r = subprocess.run([str(binary), *map(str, args)], env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=30)
            assert r.returncode == code, (r.returncode, r.stdout, r.stderr)
            return r.stdout.decode()

        source = invoke('--judge-probe', profile)
        assert source == invoke('--judge-probe', profile)
        assert source.index('// presence complete') < source.index('#define ')
        pasted = root / 'judge.txt'
        pasted.write_text('not a probe\n', encoding='ascii')
        assert 'no expected output shipped for linux-gcc15-gnupp23; run profiles.yml' in invoke('--judge-check', profile, pasted, code=1)
        header, manifest = re.findall(r'__distill_probe_line\("([^"\n]+)"\);', source)
        keys = re.findall(r'__distill_probe_emit\("([^"\n]+)"[,]', source)
        # Protocol-only fixture: the observation checker is not an authenticator.
        # A fabricated expected file is sufficient here; no judge facts are imported.
        lines = [header, manifest, *[key + '|30' for key in keys]]
        payload = ('\n'.join(lines) + '\n').encode()
        checksum = 2166136261
        for c in payload:
            checksum = ((checksum ^ c) * 16777619) & 0xffffffff
        payload += f'END|{len(lines)}|{checksum}\n'.encode()
        expected = root / 'linux-gcc15-gnupp23-probe-expected.txt'
        expected.write_bytes(payload)
        pasted.write_bytes(payload)
        assert 'user-reported match' in invoke('--judge-check', profile, pasted, '--record', root / 'observations')
        assert len(list((root / 'observations').glob('*.txt'))) == 1
        pasted.write_bytes(payload[:-1])
        assert 'format error' in invoke('--judge-check', profile, pasted, code=1)
        invoke('--judge-probe', profile, '--part', '0/2', code=1)


if __name__ == '__main__':
    smoke(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
