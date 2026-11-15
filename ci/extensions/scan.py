# SPDX-License-Identifier: GPL-3.0-or-later
"""Select the header roots for one compatibility-matrix toolchain."""
import argparse
from pathlib import Path
import shutil
import subprocess
from generate import generate, ROOT


def run(argv):
    return subprocess.run(argv, check=True, text=True, encoding='utf-8', errors='replace',
                          capture_output=True, timeout=30)


def scan(platform, compiler, output_dir, headers_version):
    output_dir.mkdir(parents=True, exist_ok=True)
    # Seed each job from the reviewed data; the final merge unions all job outputs.
    for library in ('glibc', 'libstdc++', 'mingw-w64', 'macos-libc'):
        destination = output_dir / (library + '.txt')
        if not destination.exists():
            shutil.copyfile(ROOT / 'data/extensions' / destination.name, destination)
    if platform == 'macos':
        sdk = Path(run(['xcrun', '--sdk', 'macosx', '--show-sdk-path']).stdout.strip())
        version = run(['xcrun', '--sdk', 'macosx', '--show-sdk-version']).stdout.strip()
        generate('macos-libc', [sdk / 'usr/include'], output_dir / 'macos-libc.txt',
                 [f'macOS SDK {version}; {headers_version}'], ['c++'])
        return
    compiler = str(Path(shutil.which(compiler) or compiler).resolve())
    version = run([compiler, '--version']).stdout.splitlines()[0]
    triple = run([compiler, '-dumpmachine']).stdout.strip()
    verbose = subprocess.run(
        [compiler, '-x', 'c++', '-E', '-v', '-'], input='', text=True, encoding='utf-8',
        errors='replace', check=True, capture_output=True, timeout=30)
    directories = []
    active = False
    for line in verbose.stderr.splitlines():
        if line.startswith('#include <...>'):
            active = True
        elif line.startswith('End of search list.'):
            active = False
        elif active and '/c++/' in line.replace('\\', '/'):
            path = Path(line.strip())
            if path.is_dir():
                directories.append(path)
    if not directories:
        raise ValueError('GCC C++ include directories not found')
    provenance = [f'{version}; {triple}; {headers_version}']
    if platform == 'linux':
        libc = run(['getconf', 'GNU_LIBC_VERSION']).stdout.strip()
        generate('glibc', [Path('/usr/include')], output_dir / 'glibc.txt', provenance + [libc], ['c++'])
        generate('libstdc++', directories, output_dir / 'libstdc++.txt', provenance)
    else:
        crt = Path(compiler).parent.parent / 'include'
        generate('mingw-w64', [crt, *directories], output_dir / 'mingw-w64.txt',
                 provenance + ['MinGW-w64 CRT and this GCC toolchain libstdc++ headers'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=('linux', 'mingw', 'macos'), required=True)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--headers-version', required=True)
    args = parser.parse_args()
    try:
        scan(args.platform, args.compiler, args.output_dir, args.headers_version)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f'header scan failed: {error}\n')
