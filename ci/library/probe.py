# SPDX-License-Identifier: GPL-3.0-or-later
"""Diagnostic companion to --judge-probe: one independent TU per standard header.

No local compiler is run. Submit generated TUs to the judge, collect stdout,
and compare with --check. Observations never update snapshots or resolver facts.
"""
import argparse
import json
from pathlib import Path
import re



def expected(report, header):
    names = sorted(set(report['audit']['macros']) |
                   set().union(*(set(h['macros']) for h in report['headers'].values())))
    snapshot = report['headers'][header]
    return {name: snapshot['macros'].get(name, '<undefined>') for name in names}


def source(report, header):
    if not re.fullmatch(r'[A-Za-z_0-9./+\-]+', header) or '..' in header.split('/'):
        raise ValueError('invalid header spelling')
    lines = ['// SPDX-License-Identifier: GPL-3.0-or-later',
             '// Diagnostic only; compile this file independently on the judge.',
             f'#if __has_include(<{header}>)', f'#include <{header}>',
             '#define __distill_lib_s1(x) #x',
             '#define __distill_lib_s(x) __distill_lib_s1(x)',
             'extern "C" int puts(const char*);', 'int main() {',
             f'puts("DLIB|{header}|<header>|ok");']
    for name in expected(report, header):
        lines += [f'#ifdef {name}', f'puts("DLIB|{header}|{name}|" __distill_lib_s({name}));',
                  '#else', f'puts("DLIB|{header}|{name}|<undefined>");', '#endif']
    lines += ['}', '#else', 'extern "C" int puts(const char*);',
              f'int main() {{ puts("DLIB|{header}|<header>|missing"); }}', '#endif']
    return '\n'.join(lines) + '\n'


def compare(report, header, output):
    actual = {}
    for line in output.splitlines():
        parts = line.split('|', 3)
        if len(parts) != 4 or parts[:2] != ['DLIB', header] or parts[2] in actual:
            raise ValueError('malformed/duplicate library probe record')
        actual[parts[2]] = parts[3]
    status = report['headers'][header]['status']
    if status not in ('ok', 'missing'):
        raise ValueError('snapshot header did not preprocess; no comparison available')
    want = {'<header>': status}
    if status == 'ok':
        want.update(expected(report, header))
    return [f'{name}: snapshot={want.get(name)!r}, judge={actual.get(name)!r}'
            for name in sorted(want.keys() | actual.keys()) if want.get(name) != actual.get(name)]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--header', required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--output', type=Path, help='write self-contained C++ TU, without running a compiler')
    mode.add_argument('--check', type=Path, help='compare judge stdout; diagnostic only')
    args = parser.parse_args()
    try:
        report = json.loads(args.snapshot.read_text())
        if args.output:
            args.output.write_text(source(report, args.header), encoding='utf-8', newline='\n')
        else:
            mismatches = compare(report, args.header, args.check.read_text())
            print('\n'.join(mismatches) if mismatches else 'user-reported library match (diagnostic only)')
            raise SystemExit(1 if mismatches else 0)
    except (OSError, KeyError, ValueError) as error:
        parser.exit(1, f'library probe failed: {error}\n')
