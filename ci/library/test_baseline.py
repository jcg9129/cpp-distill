# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the public encoder/decoder directly, without a converter script."""
import copy
import json
import random

import pytest

from baseline import (baseline_text, decode, digest, encode, pack_changes,
                      record_text, unpack_changes)
from embed import generate, ROOT
from test_embed import combined, report, standard_report


@pytest.fixture(params=['feature', 'standard', 'legacy', 'empty', 'large'])
def logical(request):
    if request.param == 'standard':
        return combined(standard_report(), standard_report('other', '2'))
    data = combined(report(), report('other', '2L'))
    if request.param == 'legacy':
        data['generator'] = 3
        del data['member_names']
        for build in data['builds'].values():
            build['generator'] = 3
            del build['member_inventory']
    elif request.param == 'empty':
        # The codec also preserves empty tables and optional-field absence.
        for table in ('macro_classifications', 'macro_sets', 'snapshots', 'agreements'):
            data[table] = {}
    elif request.param == 'large':
        rng = random.Random(111)
        names = [f'dep_{i:04}' for i in range(1101)] + ['quote"', 'é', '𐐀']
        for i in range(100):
            deps = sorted(rng.sample(names, rng.randrange(500)))
            record = dict(independent=False, values=['1L', '2L'], dependencies=deps)
            data['macro_classifications'][digest(record)[:16]] = record
        # Include changes, removals, optional fields, and shared empty sets.
        data['macro_sets']['removed'] = dict(entries={}, removed=['__cpp_lib_test'])
        data['snapshots']['optional'] = dict(status='ok', macros={}, function_like=[], noninteger=[], note='é')
    return data


def test_converter_free_lossless_round_trip(logical):
    original = copy.deepcopy(logical)
    encoded = encode(logical)
    assert encoded['schema'] == 3
    assert decode(encoded) == original
    assert logical == original  # Encoding must not modify its input.
    assert decode(json.loads(baseline_text(logical))) == original
    assert encode(decode(encoded)) == encoded
    assert decode(original) == original  # Old baselines remain readable.


def reverse_objects(value):
    if isinstance(value, dict):
        return {k: reverse_objects(v) for k, v in reversed(list(value.items()))}
    if isinstance(value, list):
        return [reverse_objects(v) for v in value]
    return value


def test_encoding_is_independent_of_object_insertion_order(logical):
    assert baseline_text(logical) == baseline_text(reverse_objects(logical))


@pytest.mark.parametrize('standard', [False, True])
def test_storage_migration_keeps_entire_embedded_source_identical(standard):
    data = combined(standard_report()) if standard else combined(report())
    headers = {'cfloat'} if standard else {'charconv'}
    assert generate(record_text(data).encode(), headers) == generate(baseline_text(data).encode(), headers)
    # The validation/transition entry point accepts stored objects too.
    from embed import transitions
    assert transitions(encode(data), headers) == transitions(data, headers)


def test_inspection_helpers_accept_both_storage_schemas():
    from merge import get_agreement, get_classification, get_snapshot
    data = combined(report())
    stored = encode(data)
    for read, args in ((get_agreement, ('14', 'charconv')),
                       (get_classification, ('14', 'charconv')),
                       (get_snapshot, ('profile-test', 'charconv'))):
        assert read(stored, *args) == read(data, *args)


def test_reviewed_baseline_round_trip_and_size():
    stored = json.loads((ROOT / 'data/library/library.json').read_bytes())
    logical = decode(stored)
    assert decode(encode(logical)) == logical
    # The imported corrected reset/argv matrix occupies 8,639,326 bytes.
    assert len(baseline_text(logical).encode()) < 9_000_000
    headers = {n for n in (ROOT / 'data/names/standard-headers.txt').read_text().splitlines()
               if n and not n.startswith('#')}
    assert generate(record_text(logical).encode(), headers) == generate(baseline_text(logical).encode(), headers)


def test_delta_codec_empty_sparse_dense_and_multibyte_gaps():
    for bits in (0, 1, 1 << 1100, (1 << 1101) - 1, (1 << 16384) | 1):
        assert unpack_changes(pack_changes(bits), 16385) == bits


@pytest.mark.parametrize('text,count', [('!', 2), ('AA==', 0), ('gA==', 2), ('gAA=', 2),
                                        ('AQ==', 1), ('AAAA', 2), ('AA===', 2)])
def test_reject_malformed_dependency_deltas(text, count):
    with pytest.raises(ValueError):
        unpack_changes(text, count)


@pytest.mark.parametrize('mutation', ['schema', 'names', 'negative', 'boolean', 'large',
                                     'dependency-order', 'macro-name', 'cycle', 'field-hash'])
def test_reject_malformed_storage(mutation):
    data = encode(combined(report()))
    if mutation == 'schema':
        data['source_schema'] = 999
    elif mutation == 'names':
        data['dependency_names'] = ['z', 'a']
    elif mutation in ('negative', 'boolean', 'large'):
        next(iter(data['macro_classifications'].values()))[2] = {'negative': -1, 'boolean': True, 'large': 999}[mutation]
    elif mutation == 'dependency-order':
        data['dependency_sets'] *= 2
    elif mutation == 'macro-name':
        next(iter(data['macro_sets'].values()))['entries'] = {'-1': 0}
    else:
        pool = data['field_sets']['snapshots.macros']
        key = next(iter(pool))
        if mutation == 'cycle':
            pool[key]['base'] = key
        else:
            pool[key]['entries']['__cpp_lib_test'] = '99L'
    with pytest.raises((ValueError, KeyError)):
        decode(data)


def test_merger_check_uses_the_new_bytes(tmp_path):
    from merge import check
    data = combined(report())
    path = tmp_path / 'library.json'
    path.write_text(baseline_text(data))
    assert check(baseline_text(data), path)
    assert decode(json.loads(path.read_text())) == data
