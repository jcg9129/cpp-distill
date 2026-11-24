#!/usr/bin/env python3
"""Download and unpack an immutable GCC 13 header package inside the worktree."""
import hashlib
from pathlib import Path
import subprocess
import urllib.request

WORK = Path('measurements/minify-analysis')
PACKAGE = 'libstdc++-13-dev_13.3.0-16_arm64.deb'
URL = 'https://deb.debian.org/debian/pool/main/g/gcc-13/' + PACKAGE
SHA256 = '5db1835e949dc2682ee27f3732af709bc6a80f9a825a70a389dd84f24ed48078'


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    package = WORK / PACKAGE
    if not package.exists():
        package.write_bytes(urllib.request.urlopen(URL, timeout=60).read())
    assert hashlib.sha256(package.read_bytes()).hexdigest() == SHA256
    subprocess.run(['dpkg-deb', '-x', str(package), str(WORK / 'gcc13')], check=True)
    print(URL)
    print(SHA256)


if __name__ == '__main__':
    main()
