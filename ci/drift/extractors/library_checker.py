# SPDX-License-Identifier: GPL-3.0-or-later
"""Moving library-checker-judge langs.toml; the cpp entry, not cpp-func."""
import tomllib
from ._support import decode, gcc_version, html, match, one, present, record
from evidence import ExtractorFailure


def extract(payload, metadata):
    html(payload, metadata)
    rows = tomllib.loads(decode(payload, metadata))['langs']
    row = one((r for r in rows if r.get('id') == 'cpp'), 'cpp language')
    version = present(row.get('version'), 'version label')
    compiler = match(r'GCC ([\d.]+) \+ (AC Library\([\d.]+\))', version, 'compiler/library label')
    args, runtime = row.get('compile'), row.get('exec')
    if (not isinstance(args, list) or not args or not isinstance(runtime, list) or not runtime
            or any(not isinstance(s, str) or not s for s in args + runtime)):
        raise ExtractorFailure('missing compile/exec argument array')
    return record(row['id'], gcc_version(compiler[1]), args, runtime, [version], label=present(row.get('name'), 'language name'))
