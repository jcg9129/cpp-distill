# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the reviewed baseline and emit only resolver header transitions."""
import hashlib
import json
from pathlib import Path
import re
import sys

from merge import canonical, get_agreement, get_classification, get_snapshot
from audit import INTEGER

ROOT = Path(__file__).resolve().parents[2]
VALUE = INTEGER
NAME = re.compile(r'__cpp_lib_[A-Za-z_0-9]+\Z')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def unique_object(pairs):
    out = {}
    for key, value in pairs:
        require(key not in out, 'duplicate JSON key: ' + key)
        out[key] = value
    return out


def standard_header(header, standard):
    # Keep the dynamic extension-list prefix aligned with names.cpp.
    return header in standard or (header.startswith('ext/pb_ds/') and '..' not in header.split('/'))


def transitions(data, standard):
    require(data['schema'] == 2 and data['generator'] == 3, 'unsupported library schema/generator')
    # Validate every content-address record, including unused audit records. Macro
    # set IDs describe their expanded maps, rather than the base/delta encoding.
    expanded = {}
    def macro_set(key, visiting):
        require(key not in visiting, 'cyclic macro set')
        if key in expanded:
            return expanded[key]
        record = data['macro_sets'][key]
        result = dict(macro_set(record['base'], visiting | {key})) if 'base' in record else {}
        for name in record.get('removed', []):
            del result[name]
        result.update(record['entries'])
        for ref in result.values():
            data['macro_classifications'][ref]
        expanded[key] = result
        return result
    def strings(value):
        return isinstance(value, list) and all(isinstance(v, str) for v in value)

    def classification(record):
        require(isinstance(record, dict) and isinstance(record['independent'], bool) and
                strings(record['values']) and strings(record['dependencies']), 'malformed classification')

    for table in ('snapshots', 'classifications', 'agreements', 'macro_classifications',
                  'macro_sets', 'build_headers', 'root_classifications', 'root_agreements'):
        require(isinstance(data[table], dict) and bool(data[table]), 'empty or malformed ' + table)
        for key, record in data[table].items():
            content = macro_set(key, set()) if table == 'macro_sets' else record
            digest = hashlib.sha256(canonical(content).encode()).hexdigest()[:16]
            require(key == digest, 'bad content address in ' + table)
    for record in data['macro_classifications'].values():
        classification(record)
    for root in data['classifications'].values():
        macro_set(root['macros'], set())
        classification(root['default'])
        require(strings(root['dependencies']), 'bad classification dependencies')
    for table, target in (('build_headers', 'snapshots'), ('root_classifications', 'classifications'),
                          ('root_agreements', 'agreements')):
        for record in data[table].values():
            require(isinstance(record, dict), 'malformed reference map')
            for header, ref in record.items():
                require(isinstance(header, str) and ref in data[target], 'invalid header reference')
    names = set()
    for record in data['agreements'].values():
        defined, absent, unknown = record['defined'], record['absent'], record['unknown']
        require(isinstance(defined, dict) and isinstance(absent, list) and isinstance(unknown, dict),
                'malformed agreement')
        require(len(set(absent)) == len(absent) and not (set(defined) & set(absent) or
                set(defined) & set(unknown) or set(absent) & set(unknown)), 'overlapping agreement states')
        names.update(defined); names.update(absent); names.update(unknown)
        require(all(isinstance(v, str) and VALUE.fullmatch(v) for v in defined.values()), 'unusable agreement value')
        require(all(isinstance(v, str) and v for v in unknown.values()), 'invalid unknown reason')
    for record in data['snapshots'].values():
        require(record['status'] in ('ok', 'missing', 'error', 'disabled') and
                isinstance(record['macros'], dict), 'malformed snapshot')
        require(record['status'] == 'ok' or not record['macros'], 'failed snapshot with facts')
        require(all(isinstance(v, str) for v in record['macros'].values()), 'non-string snapshot value')
        names.update(record['macros'])
    require(names and all(NAME.fullmatch(n) for n in names), 'invalid feature name')
    names = sorted(names)
    rows, builds = {}, []
    require(data['versions'].keys() == data['agreement'].keys(), 'incomplete major agreement')
    for major, version in sorted(data['versions'].items()):
        require(re.fullmatch(r'[1-9][0-9]*', major), 'invalid GCC major')
        roots = data['root_classifications'][version['roots']]
        agreements = data['root_agreements'][data['agreement'][major]]
        require(roots.keys() == agreements.keys(), 'incomplete header agreement')
        require(version['builds'] and all(data['builds'][b]['major'] == major for b in version['builds']),
                'invalid major builds')
        for header in roots:
            get_classification(data, major, header)  # Check every reference.
            a = get_agreement(data, major, header)
            if standard_header(header, standard):
                rows['gcc-' + major, header] = tuple(a['defined'].get(n, 0 if n in a['absent'] else 1) for n in names)
    for key, build in sorted(data['builds'].items()):
        require(build['schema'] == 2 and build['generator'] == 3 and build['build'] == key and
                isinstance(build['reproduced'], bool), 'invalid build metadata')
        major = build['major']
        require(key in data['versions'][major]['builds'], 'unlisted build')
        require(re.fullmatch(r'[0-9a-f]{64}', build['evidence_sha256']), 'invalid evidence digest')
        headers = data['build_headers'][build['headers']]
        require(strings(build['volatile']) and strings(build['flags']), 'invalid build flags/volatile inputs')
        require(all(isinstance(build[k], str) and build[k] for k in ('gcc', 'std', 'triple')),
                'invalid build identity')
        volatile = set(build['volatile'])
        for header in headers:
            snapshot = get_snapshot(data, key, header)
            if not key.startswith('profile-') or not build['reproduced'] or not standard_header(header, standard):
                continue
            classification = get_classification(data, major, header)
            values = []
            for name in names:
                entry = classification['macros'].get(name, classification['default'])
                value = snapshot['macros'].get(name, 0)
                # Own build-dependent values are allowed, but native inputs still
                # vary by host. Major dependencies conservatively overapproximate
                # the own build's native dependencies, including absence claims.
                if snapshot['status'] != 'ok' or volatile.intersection(entry['dependencies']) or (
                        isinstance(value, str) and not VALUE.fullmatch(value)):
                    value = 1
                values.append(value)
            rows[key, header] = tuple(values)
        own = key.startswith('profile-') and build['reproduced']
        if own:
            require(isinstance(build['provenance']['build'], str), 'invalid build provenance')
        # Agreement coverage needs every scanned build, including approximate
        # profiles and extension scans. Only reproduced profiles have snapshots.
        builds.append((key if own else '', major, build['gcc'], build['std'], build['triple'],
                       build['provenance']['build'] if own else '', build['flags']))
    require(rows, 'no library transitions')
    return names, rows, builds


def generate(raw, standard):
    data = json.loads(raw, object_pairs_hook=unique_object)
    names, rows, builds = transitions(data, standard)
    values = sorted({v for row in rows.values() for v in row if isinstance(v, str)})
    require(len(values) + 2 <= 65536, 'too many values')
    indexes = {v: i + 2 for i, v in enumerate(values)}
    records = sorted(set(rows.values()), key=lambda row: canonical(row))
    record_ids = {row: i for i, row in enumerate(records)}
    quote = lambda s: json.dumps(s, ensure_ascii=True)
    out = ['// SPDX-License-Identifier: GPL-3.0-or-later', '// Generated by ci/library/embed.py; do not edit.',
           '#include "core/library/library.h"', 'namespace distill { namespace {',
           'constexpr std::string_view names[] = {' + ','.join(map(quote, names)) + '};',
           'constexpr std::string_view values[] = {"","",' + ','.join(map(quote, values)) + '};']
    for i, row in enumerate(records):
        out.append('constexpr std::uint16_t s' + str(i) + '[] = {' +
                   ','.join(str(indexes[v] if isinstance(v, str) else v) for v in row) + '};')
    out.append('constexpr LibraryHeader headers[] = {')
    for (selector, header), row in sorted(rows.items()):
        out.append('{' + quote(selector) + ',' + quote(header) + ',s' + str(record_ids[row]) + '},')
    out.append('};')
    for i, b in enumerate(builds):
        # GCC accepts zero-sized arrays only as an extension; an empty span is portable.
        if b[-1]:
            out.append('constexpr std::string_view f' + str(i) + '[] = {' + ','.join(map(quote, b[-1])) + '};')
    if builds:
        out.append('constexpr LibraryBuild builds[] = {')
    for i, b in enumerate(builds):
        out.append('{' + ','.join(map(quote, b[:-1])) + ',' + ('f' + str(i) if b[-1] else '{}') + '},')
    provenance = ('ci/library generator=3 schema=2 embed=1 sha256:' + hashlib.sha256(raw).hexdigest())
    if builds:
        out.append('};')
    out += ['} const LibraryData& embeddedLibraryData() {',
            'static const LibraryData data{names,values,headers,' + ('builds' if builds else '{}') + ',' + quote(provenance) + '};',
            'return data;', '}', '}']
    return '\n'.join(out) + '\n'


def main():
    standard = {s for s in (ROOT / 'data/names/standard-headers.txt').read_text().splitlines()
                if s and not s.startswith('#')}
    output = generate(Path(sys.argv[1]).read_bytes(), standard)
    Path(sys.argv[2]).write_text(output, encoding='utf-8', newline='\n')
    print(f'Embedded library transitions: {len(output.encode())} source bytes')


if __name__ == '__main__':
    try:
        main()
    except (KeyError, TypeError, ValueError, OSError, RecursionError) as error:
        sys.exit('invalid library baseline: ' + str(error))
