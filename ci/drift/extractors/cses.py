# SPDX-License-Identifier: GPL-3.0-or-later
"""CSES how-to language/version/flags table; only flags are published."""
from ._support import command, html, match, one, record
from evidence import ExtractorFailure


def extract(payload, metadata):
    root = html(payload, metadata)
    row = one((r for r in root.all('tr') if r.cells() and r.cells()[0].text() == 'C++'), 'C++ language row')
    cells = row.cells()
    if len(cells) != 4:
        raise ExtractorFailure('missing language/version/flags/packages cell')
    version = match(r'g\+\+ ([\d.]+)', cells[1].text(), 'compiler version')[1]
    flags = one((c.raw() for c in cells[2].all('code') if '-std=c++23' in command(c.raw())), 'C++23 flags')
    return record('C++23', version, command(flags), libraries=[cells[3].text()] if cells[3].text() else [], label='C++23')
