# SPDX-License-Identifier: GPL-3.0-or-later
"""Resolver table tests: synthetic data plus round trips of the reviewed baseline."""
import hashlib
import json
import shutil
import subprocess
import sys

import pytest

from audit import classify, VERSION
from embed import generate, transitions, ROOT, MAX_SOURCE_BYTES
from merge import merge, baseline_text, get_agreement, get_snapshot, standard_scope, harmless_flag
from member_inventory import inventory
from baseline import decode, record_text

NAME = '__cpp_lib_test'


def report(build='profile-test', value='1L', condition='', volatile=(), reproduced=True, status='ok'):
    from member_inventory import inventory
    text = '#define ' + NAME + ' 1L'
    if condition:
        text = '#if ' + condition + '\n' + text + '\n#endif'
    return dict(schema=2, generator=VERSION, build=build, gcc='14.2.0', major='14',
                member_inventory=inventory({'0/charconv': text}),
                std='c++23', triple='x86_64-pc-linux-gnu', flags=[], compiler='synthetic',
                reproduced=reproduced, provenance=dict(build='recipe=https://example.test'),
                volatile=list(volatile), observations={},
                headers={'charconv': dict(status=status, macros={} if value is None or status != 'ok' else {NAME: value})},
                audit=classify({'0/charconv': text}, (), roots=['charconv']))


def combined(*reports):
    return merge(list(reports), [r['build'] for r in reports])


def test_embed_agreement_disagreement_and_own_build_snapshot():
    a, b = report(), report('other', '2L', reproduced=False)
    data = combined(a, b)
    names, rows, builds = transitions(data, {'charconv'})
    assert names == [NAME]
    assert rows['gcc-14', 'charconv'] == (1,)
    assert rows['profile-test', 'charconv'] == ('1L',)
    assert len(builds) == 2
    assert [b[0] for b in builds] == ['', 'profile-test']
    with pytest.raises(ValueError, match='no library transitions'):
        transitions(data, {'different'})
    text = generate(baseline_text(data).encode(), {'charconv'})
    assert 'LibraryHeader headers' in text and '"profile-test"' in text
    assert 'LibraryBuild builds' in text and 'schema=2' in text
    assert 'evidence_sha256' not in text and 'observations' not in text


def test_embed_reproduced_build_dependence_but_not_native_uncertainty():
    for condition, volatile, expected in [('_GLIBCXX_HAS_GTHREADS', (), '1L'),
                                          ('__AVX2__', ('__AVX2__',), 1)]:
        data = combined(report(condition=condition, volatile=volatile))
        _, rows, _ = transitions(data, {'charconv'})
        assert rows['gcc-14', 'charconv'] == (1,)
        assert rows['profile-test', 'charconv'] == (expected,)


@pytest.mark.parametrize('status', ['missing', 'error', 'disabled'])
def test_embed_failed_snapshot_does_not_prove_absence(status):
    data = combined(report(status=status))
    _, rows, _ = transitions(data, {'charconv'})
    assert rows['profile-test', 'charconv'] == (1,)
    assert rows['gcc-14', 'charconv'] == (1,)


def test_embed_absent_preserves_and_nonliteral_becomes_unknown():
    data = combined(report(value=None))
    _, rows, _ = transitions(data, {'charconv'})
    assert rows['profile-test', 'charconv'] == (0,)
    assert rows['gcc-14', 'charconv'] == (0,)
    data = combined(report(value='unknown_alias'))
    _, rows, _ = transitions(data, {'charconv'})
    assert rows['profile-test', 'charconv'] == (1,)


def test_embed_approximate_build_never_emits_own_snapshot():
    data = combined(report(reproduced=False))
    _, rows, builds = transitions(data, {'charconv'})
    assert len(builds) == 1 and builds[0][0] == ''
    assert builds[0][1:4] == ('14', '14.2.0', 'c++23')
    assert list(rows) == [('gcc-14', 'charconv')]
    text = generate(baseline_text(data).encode(), {'charconv'})
    assert 'LibraryBuild builds[]' in text
    assert '{"","14","14.2.0","c++23"' in text
    assert ',{}}' in text  # Empty flags use a portable empty span.


def test_embed_baseline_round_trip_is_small_and_deterministic():
    raw = (ROOT / 'data/library/library.json').read_bytes()
    data = decode(json.loads(raw))
    standard = {s for s in (ROOT / 'data/names/standard-headers.txt').read_text().splitlines()
                if s and not s.startswith('#')}
    names, rows, builds = transitions(data, standard)
    for (key, header), row in rows.items():
        if key.startswith('gcc-'):
            major = key[4:].split('@')[0]
            feature_agreement = get_agreement(data, major, header)
            standard_agreement = get_agreement(data, key if '@' in key else major, header)
            for name, value in zip(names, row):
                agreement = feature_agreement if name.startswith('__cpp_lib_') else standard_agreement
                assert value == agreement['defined'].get(name, 0 if name in agreement['absent'] else 1)
        else:
            snapshot = get_snapshot(data, key, header)
            assert all(value == 1 or value == snapshot['macros'].get(n, 0) for n, value in zip(names, row))
    assert {b[0] for b in builds if b[0]} == {k for k, b in data['builds'].items() if k.startswith('profile-') and b['reproduced']}
    assert len(builds) == len(data['builds'])
    for embedded, (_, build) in zip(builds, sorted(data['builds'].items())):
        assert embedded[1:5] == (build['major'], build['gcc'], build['std'], build['triple'])
        assert embedded[-1] == build['flags']
    text = generate(raw, standard)
    assert text == generate(raw, standard)
    # Widened header facts can add standard names and distinct header rows.
    # CI's next reviewed capture determines the actual new baseline size.
    assert len(text.encode('utf-8')) <= MAX_SOURCE_BYTES
    source_hash = hashlib.sha256(record_text(data).encode() if json.loads(raw)['schema'] == 3 else raw).hexdigest()
    assert source_hash in text
    # Dictionary order is irrelevant to table layout; exact bytes still identify
    # distinct baselines in provenance.
    reordered = json.dumps(data, sort_keys=True).encode()
    other = generate(reordered, standard)
    assert text.replace(source_hash, '') == other.replace(hashlib.sha256(reordered).hexdigest(), '')


@pytest.mark.parametrize('baseline', [False, True])
def test_embed_compiled_member_inventory_round_trip(tmp_path, baseline):
    compiler = shutil.which('g++') or shutil.which('clang++')
    if compiler is None:
        pytest.skip('C++20 compiler unavailable')
    data = decode(json.loads((ROOT / 'data/library/library.json').read_bytes())) if baseline else combined(report())
    if not baseline:
        # Short suffixes, long prefixes, UTF-8 prefixes, and multiple chunks.
        data['member_names'] = sorted({'a', 'aa', 'café', 'caféine', '𐐀', '𐐀x',
                                       'long_' + 'x' * 200 + 'a', 'long_' + 'x' * 200 + 'b'} |
                                      {f'member_{i:05}' for i in range(2000)} |
                                      set(data['member_names']))
    standard = {s for s in (ROOT / 'data/names/standard-headers.txt').read_text().splitlines()
                if s and not s.startswith('#')}
    generated = tmp_path / 'embedded.cpp'
    generated.write_text(generate(baseline_text(data).encode(), standard), encoding='utf-8')
    driver = tmp_path / 'main.cpp'
    driver.write_text('''#include "core/library/library.h"
#include <iostream>
int main() {
  const auto& data = distill::embeddedLibraryData();
  if (&data != &distill::embeddedLibraryData()) return 1;
  for (auto name : data.memberNames) std::cout << name << '\\n';
}
''')
    binary = tmp_path / 'inventory'
    subprocess.run([compiler, '-std=c++20', '-I', str(ROOT / 'src'), str(generated),
                    str(driver), '-o', str(binary)], check=True, capture_output=True, timeout=60)
    result = subprocess.run([str(binary)], check=True, capture_output=True, timeout=30)
    assert result.stdout.decode('utf-8').splitlines() == data['member_names']


@pytest.mark.parametrize('mutation', ['schema', 'generator', 'missing', 'address', 'reference', 'cycle'])
def test_embed_rejects_malformed_baseline(mutation):
    data = combined(report())
    if mutation in ('schema', 'generator'):
        data[mutation] = 999
    elif mutation == 'missing':
        del data['agreements']
    elif mutation == 'address':
        next(iter(data['snapshots'].values()))['macros'][NAME] = '999L'
    elif mutation == 'reference':
        data['agreement']['14'] = 'missing'
    else:
        key = next(iter(data['macro_sets']))
        data['macro_sets'][key]['base'] = key
    with pytest.raises((KeyError, ValueError)):
        generate(json.dumps(data).encode(), {'charconv'})
    with pytest.raises(ValueError, match='duplicate JSON key'):
        generate(b'{"schema":2,"schema":2}', {'charconv'})


def test_embed_cli_missing_or_malformed_data_fails_without_output(tmp_path):
    for contents in (None, '{}', '{broken'):
        input_file, output_file = tmp_path / 'library.json', tmp_path / 'generated.cpp'
        if contents is not None:
            input_file.write_text(contents)
        result = subprocess.run([sys.executable, str(ROOT / 'ci/library/embed.py'),
                                 str(input_file), str(output_file)], capture_output=True, text=True)
        assert result.returncode and 'invalid library baseline:' in result.stderr
        assert not output_file.exists()


def test_embed_includes_dynamic_standard_pbds_prefix():
    r = report()
    r['headers']['ext/pb_ds/assoc_container.hpp'] = dict(status='ok', macros={NAME: '1L'})
    r['audit'] = classify({'0/charconv': '#define ' + NAME + ' 1L',
                           '0/ext/pb_ds/assoc_container.hpp': '#define ' + NAME + ' 1L'},
                          (), roots=r['headers'])
    _, rows, _ = transitions(combined(r), {'charconv'})
    assert rows['gcc-14', 'ext/pb_ds/assoc_container.hpp'] == ('1L',)
    assert rows['profile-test', 'ext/pb_ds/assoc_container.hpp'] == ('1L',)


def standard_report(build='profile-test', value='0', *, reproduced=True):
    data = report(build, reproduced=reproduced)
    text = '#undef FLT_EVAL_METHOD\n#define FLT_EVAL_METHOD __FLT_EVAL_METHOD__'
    data['headers'] = {'cfloat': dict(status='ok', macros={'FLT_EVAL_METHOD': value})}
    data['audit'] = classify({'0/cfloat': text},
                             {'__FLT_EVAL_METHOD__': '#define __FLT_EVAL_METHOD__ ' + value}, roots=['cfloat'])
    data['member_inventory'] = inventory({'0/cfloat': text})
    return data


def test_embed_standard_integer_values_agreement_and_reproduced_argv():
    a = standard_report()
    b = standard_report('other', '2', reproduced=False)
    b['flags'] = ['-m32', '-mfpmath=387']
    data = combined(a, b)
    names, rows, builds = transitions(data, {'cfloat'})
    assert names == ['FLT_EVAL_METHOD']
    assert rows['gcc-14', 'cfloat'] == (1,)
    assert rows['profile-test', 'cfloat'] == ('0',)
    assert next(b for b in builds if not b[0])[-1] == ['-m32', '-mfpmath=387']
    text = generate(baseline_text(data).encode(), {'cfloat'})
    assert 'generator=5 schema=2' in text
    assert text.endswith(',true};\nreturn data;\n}\n}\n')
    data = combined(standard_report(), standard_report('other', reproduced=False))
    _, rows, _ = transitions(data, {'cfloat'})
    assert rows['gcc-14', 'cfloat'] == ('0',)


@pytest.mark.parametrize('level', ['-O', '-O0', '-O1', '-O2', '-O3', '-O42',
                                  '-Os', '-Ofast', '-Og', '-Oz'])
def test_standard_scope_keeps_only_last_optimization_level(level):
    build = report()
    build['flags'] = [level, '-O2', '-Wall']
    assert standard_scope(build)[-1] == ('-O2',)
    build['flags'] = ['-O2', '-fno-inline', level, '-Wall']
    assert standard_scope(build)[-1] == ('-fno-inline', level)
    build['flags'] = [level, '-fno-inline', '-O2', '-fno-inline']
    assert standard_scope(build)[-1] == ('-fno-inline', '-O2', '-fno-inline')


@pytest.mark.parametrize('flag', ['-Oother', '-O2extra', '-O-1', '-O2.0', '-O²', '-O٢'])
def test_standard_scope_preserves_non_level_optimization_spellings(flag):
    build = report()
    build['flags'] = [flag, '-O3', flag, '-O2', '-Wall']
    assert standard_scope(build)[-1] == (flag, flag, '-O2')


def test_standard_scope_current_build_keys_unchanged():
    data = json.loads((ROOT / 'data/library/library.json').read_bytes())
    for build in data['builds'].values():
        # The previous scope key kept every non-harmless flag in order.
        old_flags = tuple(f for f in build['flags'] if not harmless_flag(f))
        assert standard_scope(build)[-1] == old_flags


def test_standard_scopes_separate_native_target_and_ordered_argv():
    ordinary = standard_report('profile-ordinary', reproduced=False)
    ordinary['flags'] = ['-O2', '-Wall']
    native = standard_report('profile-native', reproduced=False)
    native['flags'] = ['-O2', '-march=native']
    native['volatile'] = ['__FLT_EVAL_METHOD__']
    native['audit'] = classify({'0/cfloat': '#define FLT_EVAL_METHOD __FLT_EVAL_METHOD__'},
                               {}, {'__FLT_EVAL_METHOD__'}, roots=['cfloat'])
    x87 = standard_report('other', '2', reproduced=False)
    x87['flags'] = ['-O2', '-m32', '-mfpmath=387']
    data = combined(ordinary, native)
    names, rows, builds = transitions(data, {'cfloat'})
    assert names == ['FLT_EVAL_METHOD']
    assert rows['gcc-14', 'cfloat'] == (1,)
    for key, expected in [('profile-ordinary', '0'), ('profile-native', 1)]:
        scope = data['builds'][key]['standard_scope']
        assert rows.get((scope, 'cfloat'), rows['gcc-14', 'cfloat']) == (expected,)
        assert get_agreement(data, scope, 'cfloat')['defined'].get('FLT_EVAL_METHOD', 1) == expected
    assert {b[-3] for b in builds} == set(data['standard_scopes'])
    assert data['builds']['profile-ordinary']['standard_inputs'] == {
        '__FLT_EVAL_METHOD__': '#define __FLT_EVAL_METHOD__ 0'}
    assert 'standard_scope' not in json.dumps(data['snapshots'])
    # A different observed value still revokes all approximate agreements,
    # including those with a matching argv and an independent own proof.
    disagrees = combined(ordinary, native, x87)
    for scope in disagrees['standard_scopes']:
        assert get_agreement(disagrees, scope, 'cfloat')['unknown']['FLT_EVAL_METHOD'] == 'cross-build-disagreement'
    # Metadata edits cannot borrow a scope captured with different flags.
    data['builds']['profile-ordinary']['flags'].append('-m32')
    with pytest.raises(ValueError, match='standard scope does not match'):
        transitions(data, {'cfloat'})


def test_standard_scope_keeps_major_feature_agreement():
    a, b = report(reproduced=False), report('other', '2L', reproduced=False)
    b['flags'] = ['-O2']
    a['headers']['charconv']['macros']['FLT_EVAL_METHOD'] = '0'
    a['audit'] = classify({'0/charconv': '#define __cpp_lib_test 1L\n#define FLT_EVAL_METHOD 0'},
                          (), roots=['charconv'])
    b['headers']['charconv']['macros']['FLT_EVAL_METHOD'] = '0'
    b['audit'] = classify({'0/charconv': '#define __cpp_lib_test 1L\n#if CONFIG\n'
                          '#define FLT_EVAL_METHOD 0\n#endif'}, (), roots=['charconv'])
    data = combined(a, b)
    names, rows, _ = transitions(data, {'charconv'})
    scope = data['builds']['profile-test']['standard_scope']
    assert rows[scope, 'charconv'][names.index('FLT_EVAL_METHOD')] == '0'
    assert rows[scope, 'charconv'][names.index(NAME)] == 1


def test_native_alternative_write_blocks_an_independent_positive_pair():
    a = standard_report(reproduced=False)
    a['flags'] = ['-march=native']
    a['volatile'] = ['__AVX2__']
    a['audit'] = classify({'0/cfloat': '#define FLT_EVAL_METHOD 0\n#if __AVX2__\n'
                          '#define FLT_EVAL_METHOD 1\n#endif'}, {}, {'__AVX2__'}, roots=['cfloat'])
    assert a['audit']['roots']['cfloat']['macros']['FLT_EVAL_METHOD']['pairs']['0']['independent']
    data = combined(a)
    assert get_agreement(data, '14', 'cfloat')['unknown']['FLT_EVAL_METHOD'] == 'config-dependent'
    _, rows, _ = transitions(data, {'cfloat'})
    assert rows['gcc-14', 'cfloat'] == (1,)


def test_compiler_input_missing_from_another_capture_remains_a_snapshot_requirement():
    a = standard_report('first', reproduced=False)
    b = standard_report('second', reproduced=False)
    b['audit'] = classify({'0/cfloat': '#define FLT_EVAL_METHOD 0'}, {}, roots=['cfloat'])
    data = combined(a, b)
    assert data['builds']['first']['standard_inputs']['__FLT_EVAL_METHOD__'] == '#define __FLT_EVAL_METHOD__ 0'
    assert data['builds']['second']['standard_inputs']['__FLT_EVAL_METHOD__'] is None


def test_standard_scope_absence_uses_major_name_universe_but_keeps_omitted_blocker():
    a = standard_report('first', reproduced=False)
    a['flags'] = ['-O2']
    b = standard_report('second', reproduced=False)
    b['headers']['cfloat']['macros'] = {}
    b['audit'] = classify({'0/cfloat': ''}, {}, roots=['cfloat'], integer_names=set())
    for build in (a, b):
        build['headers']['cmath'] = dict(status='ok', macros={})
        build['audit']['roots']['cmath'] = dict(macros={}, taint=[])
    data = combined(a, b)
    scope = data['builds']['second']['standard_scope']
    assert get_agreement(data, scope, 'cmath')['absent'] == ['FLT_EVAL_METHOD']
    b['audit'] = classify({'0/cfloat': '', '0/cmath': '#if __AVX2__\n#define FLT_EVAL_METHOD(x) x\n#endif'},
                          {}, {'__AVX2__'}, roots=['cfloat', 'cmath'], integer_names=set())
    b['volatile'] = ['__AVX2__']
    data = combined(a, b)
    assert get_agreement(data, scope, 'cmath')['unknown']['FLT_EVAL_METHOD'] == 'config-dependent'
    other_scope = data['builds']['first']['standard_scope']
    assert get_agreement(data, other_scope, 'cmath')['unknown']['FLT_EVAL_METHOD'] == 'config-dependent'


@pytest.mark.parametrize('value', ['(0)', '-1', '0 + 0', '__unknown', '0.0', '08', '1uu'])
def test_embed_standard_noninteger_replacements_stay_unknown(value):
    data = combined(standard_report(value=value))
    _, rows, _ = transitions(data, {'cfloat'})
    assert rows['gcc-14', 'cfloat'] == (1,)
    assert rows['profile-test', 'cfloat'] == (1,)


def test_embed_older_format_cannot_license_standard_values():
    data = combined(standard_report())
    data['generator'] = 4
    with pytest.raises(ValueError, match='standard macro facts require generator 5'):
        transitions(data, {'cfloat'})


def test_embed_native_input_cannot_hide_behind_standard_value_aliases():
    for alias in ['', '#define __eval_alias __AVX2__\n']:
        data = standard_report(value='1')
        text = alias + '#define FLT_EVAL_METHOD ' + ('__eval_alias' if alias else '__AVX2__')
        data['audit'] = classify({'0/cfloat': text}, {}, {'__AVX2__'}, roots=['cfloat'])
        data['volatile'] = ['__AVX2__']
        _, rows, _ = transitions(combined(data), {'cfloat'})
        assert rows['profile-test', 'cfloat'] == (1,)


def test_embed_enforces_source_size_budget(monkeypatch):
    monkeypatch.setattr('embed.MAX_SOURCE_BYTES', 1)
    with pytest.raises(ValueError, match='source exceeds size budget'):
        generate(baseline_text(combined(report())).encode(), {'charconv'})


def test_embed_reproduced_function_like_name_is_unknown_not_absent():
    a = standard_report()
    a['headers']['cfloat'].update(macros={}, function_like=['FLT_EVAL_METHOD'])
    _, rows, _ = transitions(combined(a), {'cfloat'})
    assert rows['gcc-14', 'cfloat'] == (1,)
    assert rows['profile-test', 'cfloat'] == (1,)


@pytest.mark.parametrize('conditional_include', [False, True])
def test_embed_native_conditions_in_unsupported_value_helpers_stay_unknown(conditional_include):
    data = standard_report(value='2')
    if conditional_include:
        files = {'0/cfloat': '#if __AVX2__\n#include <helper>\n#endif\n'
                             '#define FLT_EVAL_METHOD __eval_alias',
                 '0/helper': '#define __eval_alias 2'}
    else:
        files = {'0/cfloat': '#if __AVX2__\n#define __eval_alias 2\n#else\n'
                             '#define __eval_alias 0\n#endif\n#define FLT_EVAL_METHOD __eval_alias'}
    data['audit'] = classify(files, {}, {'__AVX2__'}, roots=['cfloat'])
    data['volatile'] = ['__AVX2__']
    _, rows, _ = transitions(combined(data), {'cfloat'})
    assert rows['profile-test', 'cfloat'] == (1,)


def test_noninteger_presence_is_unknown_for_both_own_and_major():
    a = standard_report()
    a['headers']['cfloat'].update(macros={}, noninteger=['FLT_EVAL_METHOD'])
    names, rows, _ = transitions(combined(a), {'cfloat'})
    assert names == ['FLT_EVAL_METHOD']
    assert rows['gcc-14', 'cfloat'] == (1,)
    assert rows['profile-test', 'cfloat'] == (1,)


def test_embed_excludes_standard_names_never_observed_as_integers():
    data = combined(standard_report(value='__unsupported'))
    text = generate(baseline_text(data).encode(), {'cfloat'})
    assert '"FLT_EVAL_METHOD"' not in text
    assert 'std::array<std::string_view,0> names{}' in text
    assert 'std::array<std::uint16_t,0> s0{}' in text
    assert text.index('#include <array>') < text.index('namespace distill')


@pytest.mark.parametrize('presence', [['FLT_EVAL_METHOD', 'FLT_EVAL_METHOD'], ['bad name'], ['assert']])
def test_embed_rejects_bad_noninteger_presence(presence):
    data = combined(standard_report())
    snapshot = next(iter(data['snapshots'].values()))
    snapshot['noninteger'] = presence
    if presence == ['assert']:
        snapshot['function_like'] = ['assert']
    # Re-address the mutated snapshot so validation reaches its contents.
    from merge import digest
    new = digest(snapshot)[:16]
    data['snapshots'] = {new: snapshot}
    mapping = next(iter(data['build_headers'].values()))
    mapping['cfloat'] = new
    new_headers = digest(mapping)[:16]
    data['build_headers'] = {new_headers: mapping}
    data['builds']['profile-test']['headers'] = new_headers
    with pytest.raises(ValueError, match='invalid noninteger presence metadata'):
        transitions(data, {'cfloat'})


@pytest.mark.parametrize('other_major', ['14', '16'])
def test_omitted_native_conditional_write_cannot_prove_own_absence(other_major):
    a = standard_report()
    a['major'], a['gcc'] = other_major, other_major + '.2.0'
    b = standard_report('profile-other')
    b['volatile'] = ['__AVX2__']
    b['headers']['cfloat']['macros'] = {}
    b['audit'] = classify({'0/cfloat': '#if __AVX2__\n#define FLT_EVAL_METHOD(x) x\n#endif'},
                          (), {'__AVX2__'}, roots=['cfloat'], integer_names=set())
    _, rows, _ = transitions(combined(a, b), {'cfloat'})
    assert rows['profile-other', 'cfloat'] == (1,)
