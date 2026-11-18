# SPDX-License-Identifier: GPL-3.0-or-later
"""Reachability inventory attributed by directory, using compiler search order."""
import argparse
from pathlib import Path
import shutil
import subprocess
from generate import generate, reachable, ROOT
from identity import scan_identities


def run(argv):
    return subprocess.run(argv, check=True, text=True, encoding='utf-8', errors='replace',
                          capture_output=True, timeout=30)


def search_paths(stderr):
    paths, active = [], False
    for line in stderr.splitlines():
        if line.startswith(('#include "..."', '#include <...>')):
            active = True
        elif line.startswith('End of search list.'):
            active = False
        elif active:
            p = Path(line.strip().removesuffix(' (framework directory)'))
            if p.is_dir() and p.resolve() not in paths:
                paths.append(p.resolve())
    if not paths:
        raise ValueError('compiler include search directories not found')
    return paths


def attribute(file, platform, search):
    spelling = file.as_posix()
    if '/include/c++/' in spelling or '/c++/v1/' in spelling:
        return 'libstdc++' if platform != 'macos' else None
    # Compiler intrinsic headers participate in traversal but are not a library.
    if '/lib/gcc/' in spelling or '/lib/clang/' in spelling:
        return None
    if platform == 'macos':
        return 'macos-libc' if '.sdk/' in spelling.lower() else None
    if any(file.is_relative_to(p) for p in search):
        return 'mingw-w64' if platform == 'mingw' else 'glibc'
    return None


def scan(platform, compiler, output_dir, headers_version):
    output_dir.mkdir(parents=True, exist_ok=True)
    compiler = str(Path(shutil.which(compiler) or compiler).resolve())
    version = run([compiler, '--version']).stdout.splitlines()[0]
    triple = run([compiler, '-dumpmachine']).stdout.strip()
    verbose = subprocess.run([compiler, '-x', 'c++', '-E', '-v', '-'], input='', text=True,
                             encoding='utf-8', errors='replace', check=True, capture_output=True, timeout=30)
    search = search_paths(verbose.stderr)
    reached = reachable(search)
    libraries = {'linux': ('glibc', 'libstdc++'), 'mingw': ('mingw-w64', 'libstdc++'), 'macos': ('macos-libc',)}[platform]
    provenance = [f'{version}; {triple}; {headers_version}']
    violations = []
    for library in libraries:
        files = {p for p in reached if attribute(p, platform, search) == library}
        if not files:
            raise ValueError('no reachable headers attributed to ' + library)
        count = generate(library, files, output_dir / (library + '.txt'), provenance)
        print(f'{library}: {len(files)} headers, {count} names')
        try:
            count = scan_identities(library, files, output_dir / (library + '-identity.json'), provenance)
            print(f'{library}: {count} identity names written (per-library exclusions only)')
        except ValueError as error:
            violations.append(str(error))
    if violations:
        raise ValueError('\n'.join(violations))


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
