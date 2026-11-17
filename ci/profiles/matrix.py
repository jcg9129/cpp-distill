# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate dispatch selection and derive compiler versions from shipped profiles."""
import argparse
import json
from pathlib import Path
import re
import tomllib
from provenance import identity


# Official Docker Hub tag API, checked 2026-11-17 (multi-platform index):
# https://hub.docker.com/v2/repositories/library/ubuntu/tags/26.04
UBUNTU_26_04 = 'ubuntu:26.04@sha256:d66b366dac61b575bee2aef07e9c26e8070096fbe84a3d4d3eb0da069b5d76db'


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
        if windows:
            acquisition = 'msys2'
        elif re.match(r'package=ubuntu-(24\.04|26\.04):g\+\+-', compiler):
            acquisition = 'ubuntu'
        elif compiler == 'recipe=https://img.atcoder.jp/file/language-update/2025-10/017-23_gcc_14-2-0.toml':
            acquisition = 'atcoder'
        else:
            raise ValueError(f'{name}: unsupported compiler acquisition: {compiler}')
        rows.append(dict(profile=name, version=version, runner='windows-2025' if windows else 'ubuntu-24.04',
                         build='' if build == 'none' else build, compiler=compiler, acquisition=acquisition))
        if compiler.startswith('package=ubuntu-26.04:'):
            rows[-1]['container'] = UBUNTU_26_04
    return {'include': rows}


def job_matrices(selection='all'):
    """Separate container rows so runner jobs never receive an empty container."""
    rows = matrix(selection)['include']
    result = {}
    for name, container in [('runner', False), ('container', True)]:
        selected = [row for row in rows if ('container' in row) == container]
        result[name + '_matrix'] = {'include': selected}
        result['has_' + name] = bool(selected)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profiles', default='all')
    args = parser.parse_args()
    try:
        print(json.dumps(matrix(args.profiles)))
    except ValueError as error:
        parser.error(str(error))
