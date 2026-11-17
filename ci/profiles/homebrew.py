# SPDX-License-Identifier: GPL-3.0-or-later
"""CI-only acquisition of a digest-pinned Homebrew GCC bottle; never builds GCC."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile
from urllib.request import Request, urlopen

from provenance import identity

BREW = Path('/home/linuxbrew/.linuxbrew/bin/brew')
VERSION = '16.2.0'
DIGEST = '6504ecae947061ffe79c9dea2f635791bf54b148d1063ec857873eac2a67d922'
PROVENANCE = f'homebrew=homebrew:gcc={VERSION}@sha256:{DIGEST}'
BANNER = f'g++-16 (Homebrew GCC {VERSION}) {VERSION}'
CLEAN_VARIABLES = ('CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH', 'GCC_EXEC_PREFIX',
                   'COMPILER_PATH', 'LIBRARY_PATH')


def clean_environment():
    env = {k: v for k, v in os.environ.items() if k not in CLEAN_VARIABLES}
    env.update(HOMEBREW_NO_AUTO_UPDATE='1', HOMEBREW_NO_INSTALL_CLEANUP='1',
               HOMEBREW_NO_INSTALL_UPGRADE='1', HOMEBREW_NO_ANALYTICS='1')
    return env


def run(*argv):
    return subprocess.run(list(map(str, argv)), check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          env=clean_environment(), timeout=900).stdout.strip()


def download(destination, digest):
    # Anonymous registry token, not a GitHub credential. Never print it.
    with urlopen('https://ghcr.io/token?scope=repository:homebrew/core/gcc:pull', timeout=60) as response:
        token = json.load(response)['token']
    request = Request('https://ghcr.io/v2/homebrew/core/gcc/blobs/sha256:' + digest,
                      headers={'Authorization': 'Bearer ' + token})
    checksum = hashlib.sha256()
    with urlopen(request, timeout=60) as response, destination.open('wb') as output:
        while chunk := response.read(1024 * 1024):
            checksum.update(chunk)
            output.write(chunk)
    if checksum.hexdigest() != digest:
        destination.unlink()
        raise ValueError('Homebrew bottle SHA-256 mismatch; refusing installation')


def install(provenance):
    # Validate the common grammar, then restrict acquisition to our audited bottle.
    identity({'snapshot_compiler': {'homebrew': provenance.removeprefix('homebrew=')}}, 'snapshot_compiler')
    if provenance != PROVENANCE:
        raise ValueError('Unsupported Homebrew bottle identity; no substitution or source fallback')
    release = Path('/etc/os-release').read_text()
    if (platform.system() != 'Linux' or platform.machine() != 'x86_64'
            or 'ID=ubuntu\n' not in release or 'VERSION_ID="24.04"' not in release):
        raise ValueError('Homebrew snapshot requires the ubuntu-24.04 x86_64 runner')
    if not BREW.is_file():
        raise ValueError(f'Linuxbrew missing: expected {BREW}; no source fallback')
    prefix = Path(run(BREW, '--prefix'))
    if prefix != BREW.parent.parent:
        raise ValueError('Unexpected Linuxbrew prefix')
    # An existing keg could cause brew to skip the supplied bottle silently.
    if (prefix / 'Cellar/gcc').exists():
        raise ValueError('Homebrew gcc already installed; refusing to substitute an existing keg')
    with tempfile.TemporaryDirectory(prefix='.homebrew-bottle-', dir=Path.cwd()) as temp:
        bottle = Path(temp) / f'gcc--{VERSION}.x86_64_linux.bottle.1.tar.gz'
        download(bottle, DIGEST)
        # Local bottle install lets brew relocate paths and install runtime deps.
        # --force-bottle forbids selecting a source build for this compiler.
        print(run(BREW, 'install', '--force-bottle', bottle), flush=True)
    if run(BREW, 'list', '--versions', 'gcc') != 'gcc ' + VERSION:
        raise ValueError('Installed Homebrew GCC version differs; no substitution allowed')
    keg = prefix / 'Cellar/gcc' / VERSION
    receipt = json.loads((keg / 'INSTALL_RECEIPT.json').read_text())
    if (receipt.get('poured_from_bottle') is not True
            or receipt.get('source', {}).get('versions', {}).get('stable') != VERSION):
        raise ValueError('GCC receipt does not identify the expected poured bottle')
    compiler = prefix / 'bin/g++-16'
    if compiler.resolve() != keg / 'bin/g++-16':
        raise ValueError('Compiler path does not resolve to the installed GCC keg')
    banner = run(compiler, '--version')
    if not banner or banner.splitlines()[0] != BANNER:
        raise ValueError('Homebrew GCC banner differs from the published judge banner')
    if run(compiler, '-dumpfullversion') != VERSION:
        raise ValueError('Homebrew GCC -dumpfullversion mismatch')
    print(banner, flush=True)
    for args in [('-v',), ('-dumpmachine',), ('-print-search-dirs',),
                 ('-E', '-v', '-x', 'c++', '/dev/null')]:
        print(f'{compiler} {" ".join(args)}\n{run(compiler, *args)}', flush=True)
    return compiler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('provenance')
    args = parser.parse_args()
    try:
        install(args.provenance)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        if isinstance(error, subprocess.CalledProcessError) and error.output:
            print(error.output, flush=True)
        parser.exit(1, f'Homebrew acquisition failed: {error}\n')


if __name__ == '__main__':
    main()
