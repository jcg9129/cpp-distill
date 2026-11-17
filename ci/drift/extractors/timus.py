# SPDX-License-Identifier: GPL-3.0-or-later
"""Timus C++ help: scoped MinGW paragraph and its batch command block."""
from ._support import command, html, match, one, record
from evidence import ExtractorFailure


def extract(payload, metadata):
    root = html(payload, metadata)
    body = one((n for n in root.all('div') if n.attrs.get('id') == 'help_text'), 'C++ help')
    paragraph = one((p for p in body.all('p') if any(b.text().startswith('MinGW GCC ') for b in p.all('b'))), 'MinGW compiler paragraph')
    label = one((b.text() for b in paragraph.all('b') if b.text().startswith('MinGW GCC ')), 'GCC label')
    version = match(r'MinGW GCC ([\d.]+) x64', label, 'GCC label')[1]
    index = body.children.index(paragraph)
    block = next((c for c in body.children[index + 1:] if getattr(c, 'tag', '') == 'pre'), None)
    if block is None:
        raise ExtractorFailure('missing batch command block')
    # Blank lines delimit the three C/GCC C++/Clang commands. Only this
    # selected code field is split; samples and historic versions are ignored.
    code = one(block.all('code'), 'batch commands').raw()
    chunks = ['\n'.join(line for line in s.splitlines() if not line.strip().startswith('//')).strip()
              for s in code.split('\n\n')]
    line = one((s for s in chunks if s.startswith('g++ ')), 'GCC C++ command')
    return record('C++23', version, command(line), [label, one(paragraph.all('a'), 'WinLibs link').text()], label=label)
