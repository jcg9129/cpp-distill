# SPDX-License-Identifier: GPL-3.0-or-later
"""yukicoder environment table and published compiler banner (Japanese)."""
from ._support import command, html, match, one, record, section
from evidence import ExtractorFailure


def extract(payload, metadata):
    root = html(payload, metadata)
    profile = metadata.get('profile')
    if profile not in ('yukicoder-gcc16-cpp23', 'yukicoder-gcc16-gnupp23'):
        raise ExtractorFailure('unknown yukicoder profile selection')
    language = 'cpp23_gnu_gcc16' if profile.endswith('-gnupp23') else 'cpp23_gcc16'
    row = one((r for r in root.all('tr') if len(r.cells()) >= 3 and r.cells()[2].text() == language), 'language ID ' + language)
    cells = row.cells()
    if len(cells) != 7 or not cells[5].text():
        raise ExtractorFailure('missing command/runtime cells')
    version = match(r'gcc ([\d.]+) \+ (boost [\d.]+ \+ ACL)', cells[1].text(), 'compiler/library version')
    _, os_section = section(root, 'h4', '実行OS環境')
    os = one(os_section.all('p'), 'runtime OS').text()
    banners = [line.strip() for p in root.all('pre') for line in p.raw().splitlines() if line.startswith('g++-16 (')]
    banner = one(banners, 'GCC 16 banner')
    return record(language, version[1], command(cells[3].raw()), [os, cells[5].text(), banner], [version[2]], label=cells[0].text())
