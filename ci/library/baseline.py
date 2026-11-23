# SPDX-License-Identifier: GPL-3.0-or-later
"""Lossless schema-3 storage for the logical schema-2 reviewed baseline.

Capture/evidence formats and content addresses describe decoded data. Keep this
codec at the serialization boundary; audits and consumers use ordinary objects.
"""
import base64
import hashlib
import json


def canonical(data):
    return json.dumps(data, sort_keys=True, separators=(',', ':'), ensure_ascii=True)


def digest(data):
    return hashlib.sha256(canonical(data).encode('utf-8')).hexdigest()


def record_text(data):
    """Sorted ASCII JSON, one table record per line (also the legacy format)."""
    sections = []
    for key, value in sorted(data.items()):
        if key == 'field_sets':
            pools = []
            for field, records in sorted(value.items()):
                entries = [f'      {json.dumps(k)}: {canonical(v)}' for k, v in sorted(records.items())]
                pools.append(f'    {json.dumps(field)}: {{\n' + ',\n'.join(entries) + '\n    }')
            sections.append(f'  {json.dumps(key)}: {{\n' + ',\n'.join(pools) + '\n  }')
        elif isinstance(value, dict):
            entries = [f'    {json.dumps(k)}: {canonical(v)}' for k, v in sorted(value.items())]
            sections.append(f'  {json.dumps(key)}: {{\n' + ',\n'.join(entries) + '\n  }')
        elif isinstance(value, list) and key == 'dependency_sets':
            sections.append(f'  {json.dumps(key)}: [\n' +
                            ',\n'.join('    ' + canonical(v) for v in value) + '\n  ]')
        else:
            sections.append(f'  {json.dumps(key)}: {canonical(value)}')
    return '{\n' + ',\n'.join(sections) + '\n}\n'


def delta_sets(sets):
    """Share repeated name maps with deterministic, acyclic base-plus-deltas."""
    result, previous = {}, []
    # Exact additive JSON costs avoid serializing every quadratic candidate.
    names = {n for entries in sets.values() for n in entries}
    weights = {n: len(canonical(n)) for n in names}
    pairs = {item for entries in sets.values() for item in entries.items()}
    pair_sizes = {(n, v): weights[n] + 1 + len(canonical(v)) for n, v in pairs}
    def map_size(items):
        return 2 + sum(map(pair_sizes.__getitem__, items)) + len(items) - bool(items)
    for key, entries in sorted(sets.items(), key=lambda kv: (len(kv[1]), canonical(kv[1]))):
        best = dict(entries=entries)
        items = set(entries.items())
        cost = 12 + map_size(items)
        for base, old, old_items, overhead in previous:
            changes = items - old_items
            removed = sorted(old.keys() - entries.keys())
            size = overhead + map_size(changes)
            if removed:
                size += 12 + sum(map(weights.__getitem__, removed)) + len(removed)
            if size < cost:
                best, cost = dict(base=base, entries=dict(changes)), size
                if removed:
                    best['removed'] = removed
        result[key] = best
        previous.append((key, entries, items, 20 + len(canonical(key))))
    return result


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sorted_names(names):
    return (isinstance(names, list) and all(isinstance(n, str) for n in names)
            and names == sorted(set(names)))


def at(values, index):
    require(type(index) is int and 0 <= index < len(values), 'invalid baseline index')
    return values[index]


def pack_changes(bits):
    """Base64 of unsigned LEB128 gaps between ascending changed bit indexes."""
    out, previous = bytearray(), -1
    while bits:
        low = bits & -bits
        index = low.bit_length() - 1
        gap, previous = index - previous - 1, index
        while gap >= 128:
            out.append((gap & 127) | 128)
            gap >>= 7
        out.append(gap)
        bits ^= low
    return base64.b64encode(out).decode('ascii')


def unpack_changes(text, count):
    require(isinstance(text, str), 'invalid dependency delta')
    raw = base64.b64decode(text, validate=True)
    result, previous, gap, shift = 0, -1, 0, 0
    for byte in raw:
        gap |= (byte & 127) << shift
        require(gap < count and shift <= count.bit_length(), 'dependency delta out of range')
        if byte & 128:
            shift += 7
            continue
        index = previous + gap + 1
        require(index < count, 'dependency delta out of range')
        result |= 1 << index
        previous, gap, shift = index, 0, 0
    require(not shift and pack_changes(result) == text, 'noncanonical dependency delta')
    return result


FIELDS = {'agreements': {'defined': 'map', 'absent': 'names', 'unknown': 'map'},
          'snapshots': {'macros': 'map', 'function_like': 'names', 'noninteger': 'names'}}


def encode(data):
    """Encode a logical baseline without mutating it or changing its hashes."""
    require(data['schema'] == 2, 'unsupported logical library schema')
    reserved = {'source_schema', 'dependency_names', 'dependency_sets', 'macro_names', 'field_sets'}
    require(not reserved.intersection(data), 'reserved baseline storage field')
    result = dict(data, schema=3, source_schema=2)
    records = data['macro_classifications']
    names = sorted({n for r in records.values() for n in r['dependencies']})
    indexes = {n: i for i, n in enumerate(names)}
    bits = {}
    for key, record in records.items():
        require(set(record) == {'independent', 'values', 'dependencies'} and
                sorted_names(record['dependencies']), 'malformed logical macro classification')
        bits[key] = sum(1 << indexes[n] for n in record['dependencies'])
    sets = sorted(set(bits.values()))
    set_ids = {b: i for i, b in enumerate(sets)}
    changes, previous = [], 0
    for b in sets:
        changes.append(pack_changes(b ^ previous))
        previous = b
    result.update(dependency_names=names, dependency_sets=changes,
                  macro_classifications={k: [r['independent'], r['values'], set_ids[bits[k]]]
                                         for k, r in records.items()})
    macro_names = sorted({n for r in data['macro_sets'].values()
                          for n in (*r['entries'], *r.get('removed', []))})
    name_ids = {n: i for i, n in enumerate(macro_names)}
    record_ids = {k: i for i, k in enumerate(sorted(records))}
    result['macro_names'] = macro_names
    result['macro_sets'] = {}
    for key, record in data['macro_sets'].items():
        row = dict(record, entries={str(name_ids[n]): record_ids[v] for n, v in record['entries'].items()})
        if 'removed' in row:
            row['removed'] = [name_ids[n] for n in row['removed']]
        result['macro_sets'][key] = row
    result['field_sets'] = {}
    for table, fields in FIELDS.items():
        pools = {field: {} for field in fields}
        result[table] = {}
        for key, record in data[table].items():
            row = dict(record)
            for field, kind in fields.items():
                if field not in record:
                    continue
                value = record[field]
                require(isinstance(value, dict) if kind == 'map' else sorted_names(value),
                        'malformed logical ' + table + '.' + field)
                ref = digest(value)[:16]
                entries = value if kind == 'map' else dict.fromkeys(value, True)
                require(ref not in pools[field] or pools[field][ref] == entries,
                        'library field digest collision')
                pools[field][ref] = entries
                row[field] = ref
            result[table][key] = row
        for field, pool in pools.items():
            result['field_sets'][table + '.' + field] = delta_sets(pool)
    return result


def expand_fields(pool, kind):
    """Iterative expansion also rejects cycles and validates unused field sets."""
    expanded = {}
    for start in pool:
        chain, visiting, key = [], set(), start
        while key not in expanded:
            require(key not in visiting, 'cyclic library field set')
            visiting.add(key)
            row = pool[key]
            chain.append((key, row))
            if 'base' not in row:
                break
            key = row['base']
        for key, row in reversed(chain):
            entries = dict(expanded[row['base']]) if 'base' in row else {}
            for name in row.get('removed', []):
                del entries[name]
            entries.update(row['entries'])
            value = entries if kind == 'map' else sorted(entries)
            require(kind == 'map' or all(v is True for v in entries.values()), 'invalid name set')
            require(digest(value)[:16] == key, 'bad content address in library field set')
            expanded[key] = entries
    return {k: v if kind == 'map' else sorted(v) for k, v in expanded.items()}


def decode(data):
    """Accept legacy schema 2 or expand schema 3 to the exact logical object."""
    if data['schema'] == 2:
        return data
    require(data['schema'] == 3 and data['source_schema'] == 2, 'unsupported library storage schema')
    result = dict(data, schema=data['source_schema'])
    for key in ('source_schema', 'dependency_names', 'dependency_sets', 'macro_names', 'field_sets'):
        del result[key]
    names = data['dependency_names']
    require(sorted_names(names), 'invalid dependency name table')
    dependencies, previous = [], 0
    require(isinstance(data['dependency_sets'], list), 'invalid dependency set table')
    for i, text in enumerate(data['dependency_sets']):
        bits = previous ^ unpack_changes(text, len(names))
        require(i == 0 or bits > previous, 'unsorted dependency sets')
        dependencies.append([n for j, n in enumerate(names) if bits & (1 << j)])
        previous = bits
    result['macro_classifications'] = {}
    for key, row in data['macro_classifications'].items():
        require(isinstance(row, list) and len(row) == 3, 'malformed encoded macro classification')
        result['macro_classifications'][key] = dict(independent=row[0], values=row[1],
                                                   dependencies=at(dependencies, row[2]))
    macro_names, record_keys = data['macro_names'], sorted(data['macro_classifications'])
    require(sorted_names(macro_names), 'invalid macro name table')
    result['macro_sets'] = {}
    for key, record in data['macro_sets'].items():
        entries = {}
        for n, v in record['entries'].items():
            require(str(int(n)) == n, 'invalid macro name index')
            entries[at(macro_names, int(n))] = at(record_keys, v)
        row = dict(record, entries=entries)
        if 'removed' in row:
            row['removed'] = [at(macro_names, n) for n in row['removed']]
        result['macro_sets'][key] = row
    for table, fields in FIELDS.items():
        pools = {f: expand_fields(data['field_sets'][table + '.' + f], kind) for f, kind in fields.items()}
        result[table] = {k: {f: pools[f][v] if f in fields else v for f, v in row.items()}
                         for k, row in data[table].items()}
    return result


def baseline_text(data):
    return record_text(encode(data))
