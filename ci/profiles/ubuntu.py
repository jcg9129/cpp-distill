# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared Ubuntu snapshot acquisition plan, also used by install-ubuntu.sh."""
import argparse
from pathlib import Path
import re

LOCK = Path(__file__).with_name('ubuntu-snapshots.txt')


def snapshot(release, lock=LOCK):
    rows = [line.split() for line in lock.read_text().splitlines()
            if line.strip() and not line.startswith('#')]
    if any(len(row) != 3 or not re.fullmatch(r'[0-9]{8}T[0-9]{6}Z', row[2]) for row in rows):
        raise ValueError('invalid Ubuntu snapshot lock')
    matches = [row for row in rows if row[0] == release]
    if len(matches) != 1:
        raise ValueError('Ubuntu release needs exactly one recorded snapshot')
    _, codename, timestamp = matches[0]
    return codename, timestamp


def packages(major):
    return [p.format(m=major) for p in (
        'g++-{m}', 'gcc-{m}', 'cpp-{m}', 'gcc-{m}-base',
        'g++-{m}-x86-64-linux-gnu', 'gcc-{m}-x86-64-linux-gnu',
        'cpp-{m}-x86-64-linux-gnu', 'libstdc++-{m}-dev', 'libgcc-{m}-dev')]


def parse(provenance):
    match = re.fullmatch(r'package=ubuntu-([0-9]{2}\.[0-9]{2}):g\+\+-([1-9][0-9]*)=((?:[0-9]+:)?[0-9][A-Za-z0-9.+~]*-[A-Za-z0-9.+~]+)', provenance)
    if not match:
        raise ValueError('unsupported Ubuntu compiler provenance')
    return match.groups()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('release')
    args = parser.parse_args()
    codename, timestamp = snapshot(args.release)
    print(codename, timestamp)
