# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture the profile dialect's compiler defaults, without judge -D flags.

Requires Python 3.11+. Exact triple and GCC version are checked before writing.
Compiler options beyond -std are deliberately outside D2c's snapshot contract.
"""
import argparse
from datetime import date
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import tomllib


def run(compiler, *args):
    return subprocess.run([compiler, *args], check=True, text=True, encoding='utf-8',
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30).stdout


def capture(profile_file, compiler):
    profile_file = Path(profile_file).resolve()
    profile = tomllib.loads(profile_file.read_text(encoding='utf-8-sig'))
    if profile['compiler']['family'] != 'gcc':
        raise ValueError('only GCC snapshots are supported')
    version = profile['compiler']['version']
    if not re.fullmatch(r'[0-9]+\.[0-9]+(?:\.[0-9]+)?', version):
        raise ValueError('profile version must be major.minor[.patch]')
    if not re.fullmatch(r'(?:c|gnu)\+\+[0-9]{2}', profile['std']):
        raise ValueError('invalid dialect')
    snapshot = profile.get('predefined', '')
    if not snapshot or snapshot in ('.', '..') or any(c in snapshot for c in '/\\:'):
        raise ValueError('predefined must name a snapshot next to the profile')
    resolved = shutil.which(str(compiler))
    if not resolved:
        raise ValueError(f'compiler not found: {compiler}')
    compiler = str(Path(resolved).resolve())
    actual_version = run(compiler, '-dumpfullversion', '-dumpversion').strip()
    expected = version.split('.')
    if actual_version.split('.')[:len(expected)] != expected:
        raise ValueError(f'GCC version mismatch: profile requires {version}, compiler reports {actual_version}')
    triple = run(compiler, '-dumpmachine').strip()
    if triple != profile['triple']:
        raise ValueError(f'target triple mismatch: profile requires {profile["triple"]}, compiler reports {triple}')
    banner = run(compiler, '--version').splitlines()[0]
    with tempfile.TemporaryDirectory(prefix='.distill-snapshot-', dir=profile_file.parent) as directory:
        empty = Path(directory) / 'empty.cpp'
        empty.write_bytes(b'')
        output = run(compiler, '-x', 'c++', '-std=' + profile['std'], '-dM', '-E', str(empty))
    lines = output.splitlines()
    if not lines or any(not line.startswith('#define ') for line in lines):
        raise ValueError('compiler did not return a macro inventory')
    macros = dict(re.findall(r'^#define ([A-Za-z_][A-Za-z_0-9]*) (.*)$', output, re.M))
    if '__clang__' in macros or macros.get('__GNUC__') != expected[0] or macros.get('__GNUC_MINOR__') != expected[1]:
        raise ValueError('predefined macros do not identify the requested GCC')
    if len(expected) == 3 and macros.get('__GNUC_PATCHLEVEL__') != expected[2]:
        raise ValueError('predefined patch level does not match the profile')
    destination = profile_file.parent / snapshot
    header = (f'# SPDX-License-Identifier: GPL-3.0-or-later\n'
              f'# compiler: {compiler}; --version: {banner}; date: {date.today().isoformat()}\n'
              f'# command: -x c++ -std={profile["std"]} -dM -E empty.cpp; judge defines NOT passed\n')
    destination.write_text(header + '\n'.join(sorted(lines)) + '\n', encoding='utf-8', newline='\n')
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', type=Path)
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    try:
        output = capture(args.profile, args.compiler)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, f'snapshot failed: {error}\n')
    print(output)


if __name__ == '__main__':
    main()
