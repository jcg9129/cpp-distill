# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate dispatch selection and derive compiler versions from shipped profiles."""
import argparse
import json
from pathlib import Path
import tomllib
from provenance import identity


def matrix(selection='all'):
    root = Path(__file__).resolve().parents[2]
    profiles = {p.stem: tomllib.loads(p.read_text(encoding='utf-8-sig')) for p in sorted((root / 'data/judges').glob('*.toml'))}
    selected = list(profiles) if selection == 'all' else [s.strip() for s in selection.split(',')]
    if not selected or any(s not in profiles for s in selected):
        raise ValueError(f'unknown or empty profile selection: {selection}; available: {", ".join(profiles)}')
    rows = []
    for name in dict.fromkeys(selected):
        p = profiles[name]
        version = p['compiler']['version']
        windows = 'mingw' in p['triple']
        build = identity(p, 'build')
        snapshot_compiler = identity(p, 'snapshot_compiler')
        compiler = build if build != 'none' else snapshot_compiler
        if windows and not compiler.startswith('package=msys2:mingw-w64-x86_64-gcc='):
            raise ValueError(f'{name}: Windows snapshot requires an exact MSYS2 GCC package')
        rows.append(dict(profile=name, version=version, runner='windows-2025' if windows else 'ubuntu-24.04',
                         build='' if build == 'none' else build, compiler=compiler))
    return {'include': rows}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profiles', default='all')
    args = parser.parse_args()
    try:
        print(json.dumps(matrix(args.profiles)))
    except ValueError as error:
        parser.error(str(error))
