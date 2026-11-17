# SPDX-License-Identifier: GPL-3.0-or-later
"""CI-only acquisition of the audited, digest-pinned Timus snapshot archive."""
import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import platform
import re
import subprocess
import tempfile
from urllib.request import urlopen
import zipfile

from provenance import identity

TAG = '13.2.0posix-17.0.6-11.0.1-ucrt-r4'
ASSET = 'winlibs-x86_64-posix-seh-gcc-13.2.0-mingw-w64ucrt-11.0.1-r4.zip'
DIGEST = '6e2bc5d50c9b2775193a5f9f41939de952ee5b6a550e9c3375bc52dda6162771'
PROVENANCE = f'winlibs=winlibs:{TAG}/{ASSET}@sha256:{DIGEST}'
URL = f'https://github.com/brechtsanders/winlibs_mingw/releases/download/{TAG}/{ASSET}'
CLEAN_VARIABLES = ('CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH', 'GCC_EXEC_PREFIX',
                   'COMPILER_PATH', 'LIBRARY_PATH')


def clean_environment(compiler):
    env = {k: v for k, v in os.environ.items() if k.upper() not in (*CLEAN_VARIABLES, 'PATH')}
    # GCC's sibling programs/DLLs come only from this archive, never MSYS2 or
    # another runner compiler. Absolute Python/compiler paths are used by CI.
    # Python uppercases environment keys on Windows; plain dicts do not retain
    # os.environ's case-insensitive lookup behavior.
    system = Path(env.get('SYSTEMROOT') or env['SystemRoot'])
    env['PATH'] = ';'.join(map(str, (compiler.parent, system / 'System32', system)))
    return env


def run(compiler, *args):
    return subprocess.run([str(compiler), *args], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          env=clean_environment(compiler), timeout=120).stdout.strip()


def download(destination):
    checksum = hashlib.sha256()
    with urlopen(URL, timeout=60) as response, destination.open('wb') as output:
        while chunk := response.read(1024 * 1024):
            checksum.update(chunk)
            output.write(chunk)
    if checksum.hexdigest() != DIGEST:
        destination.unlink()
        raise ValueError('WinLibs archive SHA-256 mismatch; refusing extraction')


def extract(archive, destination):
    with zipfile.ZipFile(archive) as zipped:
        for member in zipped.infolist():
            path = PurePosixPath(member.filename)
            if (path.is_absolute() or '..' in path.parts or ':' in member.filename
                    or '\\' in member.filename or not path.parts or path.parts[0] != 'mingw64'):
                raise ValueError('Unexpected WinLibs archive path: ' + member.filename)
        zipped.extractall(destination)


def verify(compiler):
    if not compiler.is_absolute() or not compiler.is_file():
        raise ValueError('Expected absolute archive compiler path is missing')
    if run(compiler, '-dumpfullversion') != '13.2.0':
        raise ValueError('WinLibs GCC -dumpfullversion mismatch')
    verbose = run(compiler, '-v')
    print(verbose, flush=True)
    # WinLibs recipes/gcc.winlib at 28d7b327ad6ade3d92a124bee1e383c6c56bd46d
    # (2024-01-12) uses --enable-threads and this pkgversion UCRT/SEH identity.
    # GCC's configure option is --enable-threads; accept the requested
    # --with-threads spelling too. WinLibs' UCRT/SEH identity is in pkgversion,
    # not necessarily a --with-default-msvcrt option in GCC's configure line.
    for pattern in (r'--(?:enable|with)-threads=posix(?:\s|$)',
                    r'MinGW-W64 x86_64-ucrt-posix-seh',
                    r'--(?:host|target)=x86_64-w64-mingw32(?:\s|$)',
                    r'Thread model: posix(?:\s|$)'):
        if not re.search(pattern, verbose):
            raise ValueError('Unexpected WinLibs GCC configuration: ' + pattern)
    # Validate header-derived CRT, MinGW version, POSIX and SEH configuration
    # separately from snapshot entry facts; these includes never enter a snapshot.
    with tempfile.TemporaryDirectory(prefix='.winlibs-check-', dir=compiler.parent.parent) as temp:
        source = Path(temp) / 'configuration.cpp'
        source.write_text('''#include <cstdio>
#include <_mingw.h>
#if !defined(_UCRT) || !defined(__MSVCRT__) || __MSVCRT_VERSION__ != 0xE00
#error expected UCRT
#endif
#if __MINGW64_VERSION_MAJOR != 11 || __MINGW64_VERSION_MINOR != 0 || __MINGW64_VERSION_BUGFIX != 1
#error expected MinGW-w64 11.0.1
#endif
#if defined(__USING_MCFGTHREAD__) || !defined(_REENTRANT) || !defined(__SEH__)
#error expected POSIX threads and SEH
#endif
''', encoding='utf-8')
        run(compiler, '-x', 'c++', '-std=c++23', '-E', str(source))
    for args in [('--version',), ('-dumpmachine',), ('-print-search-dirs',),
                 ('-E', '-v', '-x', 'c++', 'NUL')]:
        output = run(compiler, *args)
        print(f'{compiler} {" ".join(args)}\n{output}', flush=True)
        if args == ('-dumpmachine',) and output != 'x86_64-w64-mingw32':
            raise ValueError('Unexpected WinLibs GCC target')


def install(provenance, destination):
    identity({'snapshot_compiler': {'winlibs': provenance.removeprefix('winlibs=')}}, 'snapshot_compiler')
    if provenance != PROVENANCE:
        raise ValueError('Unsupported WinLibs archive identity; no substitution allowed')
    if platform.system() != 'Windows' or platform.machine().lower() not in ('amd64', 'x86_64'):
        raise ValueError('WinLibs snapshot requires a Windows x86_64 runner')
    destination = Path(destination).resolve()
    # Refuse an existing tree rather than mixing compiler/header installations.
    destination.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='.winlibs-download-', dir=destination) as temp:
        archive = Path(temp) / ASSET
        download(archive)
        extract(archive, destination)
    compiler = destination / 'mingw64/bin/g++.exe'
    verify(compiler)
    return compiler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('provenance')
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    try:
        install(args.provenance, args.destination)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        if isinstance(error, subprocess.CalledProcessError) and error.output:
            print(error.output, flush=True)
        parser.exit(1, f'WinLibs acquisition failed: {error}\n')


if __name__ == '__main__':
    main()
