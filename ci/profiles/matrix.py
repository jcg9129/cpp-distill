# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate dispatch selection and derive compiler versions from shipped profiles."""
import argparse
import json
from pathlib import Path
import tomllib

# Docker Hub's published multi-architecture manifest digest, checked 2026-11-15.
GCC_IMAGES = {
    '15.2.0': 'gcc:15.2.0@sha256:3ae15afe768b06d0c0fe088d822ba5f8045c26630bdacc8d8e7713cf5d8e7289',
}


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
        if not windows and version not in GCC_IMAGES:
            raise ValueError(f'add an exact GCC image pin for {name} {version}')
        rows.append(dict(profile=name, version=version, runner='windows-2025' if windows else 'ubuntu-24.04',
                         image='' if windows else GCC_IMAGES[version]))
    return {'include': rows}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profiles', default='all')
    args = parser.parse_args()
    try:
        print(json.dumps(matrix(args.profiles)))
    except ValueError as error:
        parser.error(str(error))
