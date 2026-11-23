# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the reviewed baseline and emit only resolver header transitions."""
import hashlib
import json
from pathlib import Path
import re
import sys

from merge import canonical, get_agreement, get_classification, get_snapshot
from audit import INTEGER, library_macro
from member_inventory import validate_names

ROOT = Path(__file__).resolve().parents[2]
# Full 14-build generator-5 matrix: 916,768 bytes after integer-only
# standard-name selection (1,654,630 before). Leave ~9% measured headroom.
MAX_SOURCE_BYTES = 1_000_000
VALUE = INTEGER
NAME = re.compile(r'[A-Za-z_][A-Za-z_0-9]*\Z')


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
    require(data['schema'] == 2 and data['generator'] in (3, 4, 5), 'unsupported library schema/generator')
    member_names(data)
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
        functions = record.get('function_like', [])
        noninteger = record.get('noninteger', [])
        require(strings(functions) and len(functions) == len(set(functions)) and
                all(NAME.fullmatch(n) and library_macro(n) for n in functions) and
                not set(functions).intersection(record['macros']) and
                (record['status'] == 'ok' or not functions), 'invalid function-like presence metadata')
        require(strings(noninteger) and len(noninteger) == len(set(noninteger)) and
                all(NAME.fullmatch(n) and library_macro(n) for n in noninteger) and
                not set(noninteger).intersection(record['macros']) and
                not set(noninteger).intersection(functions) and
                (record['status'] == 'ok' or not noninteger), 'invalid noninteger presence metadata')
        names.update(record['macros'])
    require(names and all(NAME.fullmatch(n) and library_macro(n) for n in names), 'invalid library macro name')
    require(data['generator'] >= 5 or all(n.startswith('__cpp_lib_') for n in names),
            'standard macro facts require generator 5')
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
        require(build['schema'] == 2 and build['generator'] == data['generator'] and build['build'] == key and
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
                if snapshot['status'] != 'ok' or name in snapshot.get('function_like', []) or volatile.intersection(entry['dependencies']) or (
                        name in snapshot.get('noninteger', []) or
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


def member_names(data):
    if data['generator'] == 3:
        require('member_names' not in data, 'inventory requires generator 4')
        return []
    names = data.get('member_names')
    require(validate_names(names), 'missing or malformed library member names')
    require(bool(data['builds']), 'inventory has no builds')
    for build in data['builds'].values():
        record = build.get('member_inventory', {})
        require(isinstance(record, dict) and
                isinstance(record.get('names_sha256'), str) and
                re.fullmatch(r'[0-9a-f]{64}', record['names_sha256']) and
                type(record.get('name_count')) is int and 0 <= record['name_count'] <= len(names) and
                type(record.get('reachable_headers')) is int and record['reachable_headers'] >= 0 and
                type(record.get('token_pasting')) is bool, 'malformed build member inventory')
    return names


def member_chunks(names):
    """Front-code UTF-8 names; bounded literals also work with MSVC's limits.

    Each record is a printable prefix length, its suffix, and a newline.
    '~' adds 93 prefix bytes; '!' through '}' terminate the length. Prefixes end
    at character boundaries so the generated source remains valid UTF-8.
    """
    chunks, chunk, previous = [], '', ''
    for name in names:
        common = 0
        while common < min(len(previous), len(name)) and previous[common] == name[common]:
            common += 1
        prefix = len(name[:common].encode('utf-8'))
        record = '~' * (prefix // 93) + chr(33 + prefix % 93) + name[common:] + '\n'
        if chunk and len((chunk + record).encode('utf-8')) > 4096:
            chunks.append(chunk)
            chunk = ''
        chunk += record
        previous = name
    if chunk:
        chunks.append(chunk)
    return chunks


def generate(raw, standard):
    data = json.loads(raw, object_pairs_hook=unique_object)
    names, rows, builds = transitions(data, standard)
    if data['generator'] >= 5:
        # The standard-name registry also contains functions, floating values,
        # expressions and constants never supplied by any scanned header.
        # They cannot provide integer facts. Leave those names to the resolver's
        # ordinary reserved/standard-name invalidation, instead of multiplying
        # every embedded header row by the entire registry.
        integers = {n for s in data['snapshots'].values() for n, v in s['macros'].items()
                    if VALUE.fullmatch(v)}
        selected = [i for i, n in enumerate(names) if n.startswith('__cpp_lib_') or n in integers]
        names = [names[i] for i in selected]
        rows = {key: tuple(row[i] for i in selected) for key, row in rows.items()}
    values = sorted({v for row in rows.values() for v in row if isinstance(v, str)})
    require(len(values) + 2 <= 65536, 'too many values')
    indexes = {v: i + 2 for i, v in enumerate(values)}
    records = sorted(set(rows.values()), key=lambda row: canonical(row))
    record_ids = {row: i for i, row in enumerate(records)}
    # UTF-8 source also handles non-BMP identifiers; JSON's surrogate-pair
    # escapes are not valid C++ universal character names.
    quote = lambda s: json.dumps(s, ensure_ascii=False)
    out = ['// SPDX-License-Identifier: GPL-3.0-or-later', '// Generated by ci/library/embed.py; do not edit.',
           '#include "core/library/library.h"', '#include <string>', '#include <vector>',
           *(['#include <array>'] if not names else []),
           'namespace distill { namespace {',
           ('constexpr std::string_view names[] = {' + ','.join(map(quote, names)) + '};'
            if names else 'constexpr std::array<std::string_view,0> names{};'),
           'constexpr std::string_view values[] = {"","",' + ','.join(map(quote, values)) + '};']
    for i, row in enumerate(records):
        out.append('constexpr std::uint16_t s' + str(i) + '[] = {' +
                   ','.join(str(indexes[v] if isinstance(v, str) else v) for v in row) + '};'
                   if row else 'constexpr std::array<std::uint16_t,0> s' + str(i) + '{};')
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
    provenance = (f'ci/library generator={data["generator"]} schema=2 embed=1 sha256:' + hashlib.sha256(raw).hexdigest())
    if builds:
        out.append('};')
    inventory = member_names(data)
    if inventory:
        out.append('constexpr std::string_view memberChunks[] = {\n' +
                   ',\n'.join(map(quote, member_chunks(inventory))) + '\n};')
        out += ['struct MemberInventory {', 'std::string storage;',
                'std::vector<std::string_view> names;', 'MemberInventory() {',
                'storage.reserve(' + str(sum(len(n.encode('utf-8')) for n in inventory)) + ');',
                'names.reserve(' + str(len(inventory)) + ');',
                '''std::size_t previous = 0;
for (auto chunk : memberChunks) while (!chunk.empty()) {
  std::size_t prefix = 0;
  while (chunk.front() == '~') { prefix += 93; chunk.remove_prefix(1); }
  prefix += chunk.front() - '!';
  chunk.remove_prefix(1);
  const auto end = chunk.find('\\n'), start = storage.size();
  storage.append(storage, previous, prefix);
  storage.append(chunk.substr(0, end));
  names.emplace_back(storage.data() + start, storage.size() - start);
  previous = start;
  chunk.remove_prefix(end + 1);
}
}
};''']
    inventory_provenance = (f'worker headers + {len(data["builds"])} scanned builds (generator {data["generator"]})'
                            if data['generator'] >= 4 else 'worker headers; scanned-build inventory unavailable')
    out += ['} const LibraryData& embeddedLibraryData() {',
            *(['static const MemberInventory inventory;'] if inventory else []),
            'static const LibraryData data{names,values,headers,' + ('builds' if builds else '{}') + ',' + quote(provenance) +
            ',' + ('inventory.names' if inventory else '{}') + ',' + quote(inventory_provenance) +
            ',' + ('true' if data['generator'] >= 5 else 'false') + '};',
            'return data;', '}', '}']
    output = '\n'.join(out) + '\n'
    require(len(output.encode('utf-8')) <= MAX_SOURCE_BYTES, 'embedded library source exceeds size budget')
    return output


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
