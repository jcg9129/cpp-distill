# SPDX-License-Identifier: GPL-3.0-or-later
"""Current AtCoder language-list details/table format (Japanese)."""
import json
from ._support import command, html, match, one, record
from evidence import ExtractorFailure


def extract(payload, metadata):
    root = html(payload, metadata)
    entry = one((n for n in root.all('details') if any(s.text().startswith('C++23 (GCC ') for s in n.all('summary'))), 'C++23 GCC entry')
    label = one(entry.all('summary'), 'language label').text()
    version = match(r'C\+\+23 \(GCC ([\d.]+)\)', label, 'compiler label')[1]
    def field(name):
        row = one((r for r in entry.all('tr') if r.cells() and r.cells()[0].text() == name), name)
        return one(row.cells()[1:], name + ' cell')
    script = one(field('コンパイルコマンド').all('code'), 'compiler script').raw()
    lines = [s.strip() for s in script.splitlines() if s.strip()]
    start = one((i for i, s in enumerate(lines) if s == 'USER_BUILD_FLAGS=('), 'literal flags array')
    args = []
    for line in lines[start + 1:]:
        end = line.endswith(')')
        item = line[:-1] if end else line
        value = json.loads(item)
        if not isinstance(value, str):
            raise ExtractorFailure('non-string compiler flag')
        args.append(value)
        if end:
            break
    else:
        raise ExtractorFailure('unterminated flags array')
    invocation = one((s for s in lines if s.startswith('g++ ')), 'GCC invocation')
    suffix = '"${USER_BUILD_FLAGS[@]}"'
    if not invocation.endswith(suffix):
        raise ExtractorFailure('unsupported GCC flags expansion')
    args = command(invocation[:-len(suffix)]) + args
    runtime = one(field('実行コマンド').all('code'), 'runtime command').text()
    acl = one((r for r in field('ライブラリ').all('tr') if any(c.text() == 'ac-library' for c in r.all('code'))), 'ACL row')
    return record('C++23', version, args, [runtime], [acl.text()], label=label)
