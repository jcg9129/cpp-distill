# SPDX-License-Identifier: GPL-3.0-or-later
"""Diagnostic companion to --judge-probe: one independent TU per standard header.

The diagnostic CLI runs no local compiler. Submit generated TUs to the judge,
collect stdout, and compare with --check. CI uses build_inputs separately to
record compiler observations. Diagnostics never update snapshots or facts.
"""
import argparse
import json
from pathlib import Path
import re


def build_inputs(compiler, std, flags, search, audit, command):
    """Record actual build facts, including context-sensitive header queries.

    Batch literal queries by lookup context. Recreate the compiler's discovered
    search order explicitly: include_next starts after the source directory,
    and quotes first search the source header's parent. No system files are
    modified and no header needs to compile for its presence to be observed.
    """
    hosted = command(compiler, ['-x', 'c++', '-std=' + std, *flags, '-dM', '-E', '-'],
                     '#include <bits/c++config.h>\n', check=False)
    definitions = dict(re.findall(r'^#define (\w+) (\S+)\s*$', hosted.stdout, re.M))
    value, seen = '_GLIBCXX_HOSTED', set()
    while value in definitions and value not in seen:
        seen.add(value)
        value = definitions[value]
    result = dict(hosted=value if hosted.returncode == 0 and value in ('0', '1') else None, headers={})
    blocked = set(audit['blocked_inputs'])
    if '_GLIBCXX_HOSTED' in blocked:
        result['hosted'] = None
    groups, values = {}, {}
    for key, test in audit['header_tests'].items():
        values[key] = []
        if test['predicate'] in blocked:
            continue
        for file in test['files']:
            index, relative = file.split('/', 1)
            begin = int(index) + 1 if test['predicate'] == '__has_include_next' else 0
            local = ((search[int(index)] / relative).parent
                     if test['operand'].startswith('"') and test['predicate'] == '__has_include' else None)
            groups.setdefault((begin, local), []).append((key, test['operand']))
    for (begin, local), tests in groups.items():
        # -I- disables stdin's implicit working-directory quote lookup. Every
        # subsequent -I is a normal search directory for both operand forms.
        argv = ['-x', 'c++', '-std=' + std, '-nostdinc', '-I-']
        if local is not None:
            argv += ['-iquote', str(local)]
        for directory in search[begin:]:
            argv += ['-I', str(directory)]
        source_ = ''.join(f'#if __has_include({operand})\nDLIB_INPUT_{i} 1\n'
                          f'#else\nDLIB_INPUT_{i} 0\n#endif\n' for i, (_, operand) in enumerate(tests))
        process = command(compiler, [*argv, '-E', '-P', '-'], source_, check=False)
        observed = dict(re.findall(r'^DLIB_INPUT_([0-9]+) ([01])$', process.stdout, re.M))
        for i, (key, _) in enumerate(tests):
            value = observed.get(str(i)) if process.returncode == 0 else None
            values[key].append(value == '1' if value is not None else None)
    for key, observations in values.items():
        result['headers'][key] = (observations[0] if observations and
                                  all(type(v) is bool and v == observations[0] for v in observations) else None)
    return result


def trusted_build_inputs(report, rows):
    """Only complete, unanimous evidence grants the two build-input rules."""
    trusted = {}
    builds = sorted(r['build'] for r in rows)
    if all(r.get('observations', {}).get('hosted') == '1' for r in rows):
        trusted['_GLIBCXX_HOSTED'] = dict(rule='all-major-builds-hosted', builds=builds, value=1)
    header_rows = [report] if report.get('reproduced') is True else rows
    for key in report['audit'].get('header_tests', {}):
        values = [r.get('observations', {}).get('headers', {}).get(key) for r in header_rows]
        if values and all(type(v) is bool and v == values[0] for v in values):
            trusted[key] = dict(rule='header-presence-agreement', value=values[0],
                                builds=sorted(r['build'] for r in header_rows))
    return trusted



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
