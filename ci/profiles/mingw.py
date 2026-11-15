# SPDX-License-Identifier: GPL-3.0-or-later
"""Download and validate the locked MSYS2 compiler dependency closure; never install."""
import argparse
from html.parser import HTMLParser
import os
from pathlib import Path, PureWindowsPath
import re
import subprocess
import sys
import tarfile
import tempfile

REPOSITORY = 'https://repo.msys2.org/mingw/mingw64/'
FILE = re.compile(r'(mingw-w64-x86_64-[A-Za-z0-9.+_-]+)-([0-9][A-Za-z0-9.+_~:-]*-[0-9]+)-any\.pkg\.tar\.zst')
DEPENDENCY = re.compile(r'(mingw-w64-x86_64-[A-Za-z0-9.+_-]+)(?:(=|>=|<=|>|<)([A-Za-z0-9.+_~:-]+))?')


def tool(name):
    if sys.platform != 'win32':
        return name
    variable = 'MSYS2_' + name.upper()
    path = os.environ.get(variable, '')
    if not PureWindowsPath(path).is_absolute():
        raise ValueError(f'{variable} must be an absolute Windows path to the MSYS2 {name} executable')
    return path


class Listing(HTMLParser):
    def __init__(self, html):
        super().__init__()
        self.files = set()
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        href = dict(attrs).get('href', '')
        if tag == 'a' and FILE.fullmatch(href):
            self.files.add(href)


def package_filename(provenance):
    match = re.fullmatch(r'package=msys2:(mingw-w64-x86_64-gcc)=([0-9][A-Za-z0-9.+_~:-]*-[0-9]+)', provenance)
    if not match:
        raise ValueError('compiler must name an exact package=msys2:mingw-w64-x86_64-gcc=VERSION-REVISION')
    return f'{match[1]}-{match[2]}-any.pkg.tar.zst'


def metadata(path):
    # Python 3.14 reads zstd directly. On CI's Python 3.12 use MSYS2 tar/zstd.
    try:
        with tarfile.open(path) as archive:
            text = archive.extractfile('.PKGINFO').read().decode('utf-8')
    except tarfile.ReadError:
        if sys.platform == 'win32':
            # Native Python does not perform MSYS2's argv/path conversion. Pass
            # archive data through handles, and never let tar find zstd on PATH.
            with path.open('rb') as source, tempfile.TemporaryFile(dir=path.parent) as archive:
                subprocess.run([tool('zstd'), '-dc'], stdin=source, stdout=archive, check=True)
                archive.seek(0)
                text = subprocess.check_output([tool('tar'), '-xOf', '-', '.PKGINFO'],
                                               stdin=archive, text=True, encoding='utf-8')
        else:
            text = subprocess.check_output(['tar', '--force-local', '-xOf', path.as_posix(), '.PKGINFO'], text=True, encoding='utf-8')
    result = {}
    for line in text.splitlines():
        if not line or line.startswith('#'):
            continue
        key, separator, value = line.partition(' = ')
        if not separator:
            raise ValueError(f'{path.name}: invalid .PKGINFO line: {line}')
        result.setdefault(key, []).append(value)
    match = FILE.fullmatch(path.name)
    if result.get('pkgname') != [match[1]] or result.get('pkgver') != [match[2]]:
        raise ValueError(f'{path.name}: .PKGINFO identity disagrees with filename')
    return result


def satisfies(actual, operator, required):
    if not operator:
        return True
    # Pacman's dependency without pkgrel compares only pkgver, including for '='.
    if '-' not in required:
        actual = actual.rsplit('-', 1)[0]
    if operator == '=':
        return actual == required
    comparison = int(subprocess.check_output([tool('vercmp'), actual, required], text=True).strip())
    return {'>': comparison > 0, '<': comparison < 0, '>=': comparison >= 0, '<=': comparison <= 0}[operator]


def closure(root, packages):
    providers = {}
    for file, info in packages.items():
        bindings = [info['pkgname'][0] + '=' + info['pkgver'][0], *info.get('provides', [])]
        for binding in bindings:
            match = DEPENDENCY.fullmatch(binding)
            if not match or match[2] not in (None, '='):
                raise ValueError(f'{file}: unsupported package/provides identity: {binding}')
            providers.setdefault(match[1], []).append((file, match[3]))
    selected, pending = set(), [root]
    while pending:
        file = pending.pop()
        if file in selected:
            continue
        selected.add(file)
        for dependency in packages[file].get('depend', []):
            match = DEPENDENCY.fullmatch(dependency)
            if not match:
                raise ValueError(f'{file}: unsupported dependency: {dependency}')
            candidates = {f for f, version in providers.get(match[1], [])
                          if not match[2] or (version and satisfies(version, match[2], match[3]))}
            if len(candidates) != 1:
                raise ValueError(f'{file}: dependency {dependency} needs exactly one matching locked package; found {len(candidates)}. Update mingw-packages.txt against the repository listing.')
            pending.extend(candidates - selected)
    if selected != packages.keys():
        raise ValueError('lock contains packages outside the compiler dependency closure: ' + ', '.join(sorted(packages.keys() - selected)))
    return sorted(selected)


def download(url, destination):
    subprocess.run([tool('curl'), '--fail', '--location', '--retry', '3', url, '-o', destination.as_posix()], check=True)


def prepare(provenance, lock, directory):
    root = package_filename(provenance)
    files = [line.strip() for line in lock.read_text().splitlines() if line.strip() and not line.startswith('#')]
    if len(files) != len(set(files)) or any(not FILE.fullmatch(f) for f in files):
        raise ValueError('invalid or duplicate exact package filename in lock')
    if root not in files:
        raise ValueError(f'compiler {root} missing from mingw-packages.txt; update its dependency lock')
    directory.mkdir(parents=True, exist_ok=True)
    index = directory / 'index.html'
    download(REPOSITORY, index)
    available = Listing(index.read_text(encoding='utf-8')).files
    for file in files:
        if file not in available:
            raise ValueError(f'Exact package unavailable: {REPOSITORY}{file}. No replacement is permitted.')
    packages = {}
    for file in files:
        path = directory / file
        download(REPOSITORY + file, path)
        packages[file] = metadata(path)
    return closure(root, packages)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('compiler', help='build identity if present, otherwise snapshot_compiler')
    parser.add_argument('--lock', type=Path, default=Path('ci/profiles/mingw-packages.txt'))
    parser.add_argument('--directory', type=Path, default=Path('out/mingw-packages'))
    args = parser.parse_args()
    try:
        # Fail before downloading if the native Windows process lacks any tool.
        for name in ('tar', 'zstd', 'curl', 'vercmp'):
            tool(name)
        files = prepare(args.compiler, args.lock, args.directory)
    except (OSError, ValueError, tarfile.TarError, subprocess.SubprocessError) as error:
        parser.exit(1, f'MSYS2 dependency resolution failed: {error}\n')
    print('\n'.join(files))


if __name__ == '__main__':
    main()
