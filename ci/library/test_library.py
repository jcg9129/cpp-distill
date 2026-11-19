# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic headers and compiler mocks only; never build or execute C++."""
import copy
import json
from pathlib import Path
import subprocess
from unittest.mock import patch

import pytest
import yaml

from audit import ROOT, classify, parse
from capture import capture, dump, header_names
from merge import merge, check, expected_builds
from probe import compare, source


def audit(text, extra=None, predefines=()):
    return classify({'0/version': text, **(extra or {})}, predefines)


def macro(text, name='__cpp_lib_test', **kwargs):
    return audit(text, **kwargs)['macros'][name]


def test_actual_stacks_gcc13_and_generated_gcc14():
    direct = macro('#if __cplusplus >= 201703L\n#define __cpp_lib_test 201611L\n#endif')
    assert direct['independent']
    assert direct['values'] == ['201611L']
    assert direct['dependencies'] == ['__cplusplus']
    generated = '''#ifndef _VERSION_H
#define _VERSION_H
#if __cplusplus >= 202002L
#define __glibcxx_test 202100L
#endif
#if defined(__glibcxx_want_all) || defined(__glibcxx_want_test)
#ifndef __cpp_lib_test
#if __glibcxx_test >= 202100L
#define __cpp_lib_test 202100L
#endif
#endif
#endif
#undef __glibcxx_want_test
#endif
'''
    # A request with no reachable write is unrecognised, rather than blindly
    # trusting a prefix. Normal header/version request writes supply evidence.
    requests = {'0/public': '#define __glibcxx_want_test\n#define __glibcxx_want_all\n#include <version>\n'}
    assert macro(generated, extra=requests)['independent']
    assert not macro(generated)['independent']
    aliased = generated.replace('#define __cpp_lib_test 202100L', '#define __cpp_lib_test __glibcxx_test')
    entry = macro(aliased, extra=requests)
    assert entry['independent'] and entry['values'] == ['202100L']
    assert macro('#if !defined(__cpp_lib_test) && __cplusplus > 0\n#define __cpp_lib_test 1L\n#endif')['independent']


@pytest.mark.parametrize('dependency', ['_GLIBCXX_USE_CXX11_ABI', '_GLIBCXX_HAS_GTHREADS',
    '_GLIBCXX_HOSTED', '_GLIBCXX_USE_TBB_PAR_BACKEND', '_GLIBCXX_HAVE_LOCALE_H',
    'UNKNOWN', '__has_include'])
def test_build_and_unknown_configuration_fail_closed(dependency):
    entry = macro(f'#if {dependency}\n#define __cpp_lib_test 1L\n#endif')
    assert not entry['independent']
    assert dependency in entry['unrecognised']


def test_elif_else_previous_tests_and_nested_inactive_conditions():
    text = '''#if _GLIBCXX_HOSTED
#define __cpp_lib_other 1L
#elif __cplusplus > 202002L
#if 0
#define __cpp_lib_test 2L
#endif
#else
#define __cpp_lib_test 1L
#endif
'''
    entry = macro(text)
    assert not entry['independent']
    assert entry['values'] == ['1L', '2L']
    assert '_GLIBCXX_HOSTED' in entry['unrecognised']


def test_comments_continuations_guards_and_own_ifndef():
    text = '''/* #define __cpp_lib_fake 1 */
#ifndef _GUARD
#define _GUARD
#ifndef __cpp_lib_test
#de\\
fine __cpp_lib_test 201703L
#endif
#endif
'''
    data = audit(text)
    assert set(data['macros']) == {'__cpp_lib_test'}
    assert '__cpp_lib_fake' not in audit('auto x = R"(\n#define __cpp_lib_fake 1L\n)";')['macros']
    assert data['macros']['__cpp_lib_test']['independent']
    assert not macro('#if __cpp_lib_test > 2\n#define __cpp_lib_test 3L\n#endif')['independent']
    for text in ['#else', '#endif', '#if 1\n#define X 1']:
        with pytest.raises(ValueError):
            parse(text, 'fixture')


def test_builtin_whitelist_is_observed_and_header_writes_revoke_it():
    text = '#if __SIZEOF_INT128__\n#define __cpp_lib_test 1L\n#endif'
    assert macro(text, predefines={'__SIZEOF_INT128__'})['independent']
    assert not macro(text)['independent']
    assert not classify({'0/version': text}, {'__SIZEOF_INT128__'},
                        {'__SIZEOF_INT128__'})['macros']['__cpp_lib_test']['independent']
    assert not macro('#define __SIZEOF_INT128__ 16\n' + text,
                     predefines={'__SIZEOF_INT128__'})['independent']


def test_pure_candidates_verified_not_assumed_and_evidence_changes():
    feature = '#if _GLIBCXX_FLOAT_IS_IEEE_BINARY32\n#define __cpp_lib_test 201611L\n#endif'
    config = '#if __FLT_MANT_DIG__ == 24\n#define _GLIBCXX_FLOAT_IS_IEEE_BINARY32 1\n#endif'
    good = audit(feature, {'0/bits/c++config.h': config}, {'__FLT_MANT_DIG__'})
    assert good['macros']['__cpp_lib_test']['independent']
    assert '_GLIBCXX_FLOAT_IS_IEEE_BINARY32' in good['support']
    for changed in [config.replace('__FLT_MANT_DIG__', '_GLIBCXX_HOSTED'),
                    config.replace(' 1\n', ' OTHER\n')]:
        assert not macro(feature, extra={'0/bits/c++config.h': changed},
                         predefines={'__FLT_MANT_DIG__'})['independent']
    assert not macro(feature)['independent']


def test_guard_shape_does_not_whitelist_a_build_switch():
    data = audit('#if _GLIBCXX_HOSTED\n#define __cpp_lib_test 1L\n#endif', {
        '0/config': '#define _GLIBCXX_HOSTED 1',
        '0/guardlike': '#ifndef _GLIBCXX_HOSTED\n#define _GLIBCXX_HOSTED 1\n#endif'})
    assert '_GLIBCXX_HOSTED' in data['macros']['__cpp_lib_test']['unrecognised']
    _, _, guard = parse('#ifndef _GUARD\n#define _GUARD 1\n#else\n#define X 1\n#endif', 'fixture')
    assert guard is None


def test_request_and_include_configuration_propagates_transitively():
    feature = '#if __glibcxx_want_test\n#define __cpp_lib_test 1L\n#endif'
    request = '#if _GLIBCXX_HAS_GTHREADS\n#define __glibcxx_want_test\n#endif'
    assert '_GLIBCXX_HAS_GTHREADS' in macro(feature, extra={'0/request': request})['unrecognised']
    parent = '#if _GLIBCXX_HOSTED\n#include <middle>\n#endif'
    assert '_GLIBCXX_HOSTED' in macro('#define __cpp_lib_test 1L', extra={
        '0/public': parent, '0/middle': '#include <version>'})['unrecognised']
    assert '<computed-include>' in macro('#define __cpp_lib_test 1L', extra={
        '0/public': '#include COMPUTED'})['unrecognised']


@pytest.mark.parametrize('text,reason', [
    ('#ifdef PB_DS_CLASS_C_DEC', 'unterminated conditional'),
    ('#else', 'unmatched else'),
    ('#elif 1', 'unmatched elif'),
    ('#endif', 'unmatched endif'),
    ('#if 1\n#else\n#else\n#endif', 'unmatched else'),
    ('#if 1\n#else\n#elif 1\n#endif', 'unmatched elif'),
    ('#define 123', 'malformed macro write'),
    ('#undef X extra', 'malformed macro write'),
])
def test_isolated_unparsed_file_is_listed_without_taint(text, reason):
    file = '0/ext/pb_ds/detail/broken.hpp'
    data = audit('#define __cpp_lib_test 1L', {file: text})
    assert data['unparsed'] == {file: file + ': ' + reason}
    assert data['macros']['__cpp_lib_test']['independent']


@pytest.mark.parametrize('operand', ['<broken>', '"broken"', '<ext/broken>'])
@pytest.mark.parametrize('kind', ['include', 'include_next'])
def test_included_unparsed_file_taints_all_results(operand, kind):
    file = '0/ext/broken'
    data = audit('#define __cpp_lib_test 1L\n#define __cpp_lib_other 2L', {
        '0/ext/parent': f'#if 0\n#{kind} {operand}\n#endif',
        file: '#if 1'})
    # Bare names resolve locally; ext/broken resolves through the search index.
    dependency = '<unparsed-file:' + file + '>'
    assert file in data['unparsed']
    for entry in data['macros'].values():
        assert not entry['independent']
        assert dependency in entry['dependencies']
        assert dependency in entry['unrecognised']


def test_include_from_unparsed_file_still_detects_unparsed_target():
    data = audit('#define __cpp_lib_test 1L', {
        '0/parent': '#else\n#include <broken>', '0/broken': '#if 1'})
    assert set(data['unparsed']) == {'0/parent', '0/broken'}
    assert data['macros']['__cpp_lib_test']['unrecognised'] == ['<unparsed-file:0/broken>']


@pytest.mark.parametrize('mention', ['__cpp_lib_hidden', '__glibcxx_hidden',
                                    '/* __cpp_lib_ */', '/* __glibcxx_ */'])
def test_unparsed_file_mentioning_feature_tokens_taints_all_results(mention):
    file = '0/broken'
    data = audit('#define __cpp_lib_test 1L', {
        file: '#if 1\n#define ' + mention + ' 2L'})
    assert file in data['unparsed']
    for entry in data['macros'].values():
        assert not entry['independent']
        assert entry['dependencies'] == ['<unparsed-file:0/broken>']
        assert entry['unrecognised'] == ['<unparsed-file:0/broken>']
    if mention == '__cpp_lib_hidden':
        # Partial writes supply no values or definitions that could be trusted.
        assert data['macros'][mention]['definitions'] == []
        assert data['macros'][mention]['values'] == []


def test_conflicting_or_nonliteral_definitions_and_undef_fail_closed():
    for text in ['#define __cpp_lib_test OTHER', '#define __cpp_lib_test(x) x',
                 '#define __cpp_lib_test 1L\n#undef __cpp_lib_test']:
        assert not macro(text)['independent']
    assert not classify({'0/version': text}, {'__SIZEOF_INT128__'},
                        {'__SIZEOF_INT128__'})['macros']['__cpp_lib_test']['independent']
    assert macro('#define __cpp_lib_test 1L\n#define __cpp_lib_test 2L')['values'] == ['1L', '2L']


def report(build='a', value='1L', status='ok', independent=True):
    return dict(schema=1, generator=1, build=build, gcc='14.2.0', major='14',
                headers={'charconv': dict(status=status, macros={} if value is None else {'__cpp_lib_test': value})},
                audit=dict(generator=1, macros={'__cpp_lib_test': dict(
                    independent=independent, values=['1L'], unrecognised=[] if independent else ['_GLIBCXX_HAS_GTHREADS'])}))


def test_agreement_presence_value_absence_and_explicit_unknown():
    good = merge([report('a'), report('b')], ['a', 'b'])
    assert good['agreement']['14']['charconv']['defined'] == {'__cpp_lib_test': '1L'}
    for value in ['2L', None]:
        data = merge([report('a'), report('b', value)], ['a', 'b'])
        assert data['agreement']['14']['charconv']['unknown']['__cpp_lib_test'] == 'cross-build-disagreement'
    data = merge([report('a', None), report('b', None)], ['a', 'b'])
    assert data['agreement']['14']['charconv']['absent'] == ['__cpp_lib_test']
    for status in ['missing', 'error', 'disabled']:
        data = merge([report('a'), report('b', status=status)], ['a', 'b'])
        assert data['agreement']['14']['charconv']['unknown']['__cpp_lib_test'] == 'unavailable-header'
    data = merge([report('a'), report('b', independent=False)], ['a', 'b'])
    assert data['versions']['14']['dependent']['__cpp_lib_test'] == ['_GLIBCXX_HAS_GTHREADS']
    assert data['agreement']['14']['charconv']['unknown']['__cpp_lib_test'] == 'config-dependent'
    # Individual snapshots retain config-dependent values for reproduced users.
    assert data['builds']['b']['headers']['charconv']['macros']['__cpp_lib_test'] == '1L'


def test_full_matrix_required_new_builds_versions_and_headers_participate():
    for reports, expected in [([report('a')], ['a', 'b']), ([report('a'), report('a')], ['a']),
                               ([report('a'), report('b')], ['a'])]:
        with pytest.raises(ValueError):
            merge(reports, expected)
    other = report('b')
    other.update(major='17', gcc='17.1.0')
    other['headers']['new'] = dict(status='missing', macros={})
    result = merge([report(), other], ['a', 'b'])
    assert set(result['versions']) == {'14', '17'}
    assert result['agreement']['17']['new']['unknown']
    assert any(b.startswith('profile-timus-') for b in expected_builds())
    assert 'extensions-mingw' in expected_builds()
    assert 'extensions-macos-arm64' not in expected_builds()


def test_diff_all_data_changes_including_new_dependency(tmp_path, capsys):
    file = tmp_path / 'library.json'
    original = merge([report()], ['a'])
    file.write_text(json.dumps(original, indent=2) + '\n')
    assert check(file.read_text(), file)
    changed = copy.deepcopy(original)
    changed['versions']['14']['dependent']['__cpp_lib_new'] = ['_GLIBCXX_NEW']
    assert not check(json.dumps(changed, indent=2) + '\n', file)
    assert '_GLIBCXX_NEW' in capsys.readouterr().out
    original['builds']['a']['audit'] = audit('#define __cpp_lib_test 1L')
    file.write_text(json.dumps(original, indent=2) + '\n')
    changed = copy.deepcopy(original)
    changed['builds']['a']['audit'] = audit('#define __cpp_lib_test 1L', {'0/broken': '#if 1'})
    assert not check(json.dumps(changed, indent=2) + '\n', file)
    assert '0/broken: unterminated conditional' in capsys.readouterr().out
    assert not check('{}\n', tmp_path / 'absent')
    file.write_bytes(b'{}\r\n')
    assert not check(b'{}\n', file)
    assert 'line endings' in capsys.readouterr().out


def test_capture_exact_flags_single_header_missing_error_empty_and_native(tmp_path):
    search = tmp_path / 'include'
    (search / 'ext/pb_ds').mkdir(parents=True)
    for name in ['charconv', 'empty', 'broken', 'ext/pb_ds/tree.hpp']:
        (search / name).write_text('')
    (search / 'charconv').write_text('#define __cpp_lib_test 1L')
    (search / 'ext/pb_ds/tree.hpp').write_text('#ifdef PB_DS_CLASS_C_DEC')
    calls = []
    def compiler(executable, args, source='', check=True):
        calls.append((args, source, check))
        if args[0] == '-dumpfullversion':
            out = '15.2.0'
        elif args[0] == '-dumpmachine':
            out = 'x86_64-linux-gnu'
        elif args[0] == '--version':
            out = 'fixture GCC'
        elif '-dD' in args:
            out = '# 0 "<built-in>"\n#define __AVX2__ 1\n#define __cplusplus 202302L\n'
        else:
            out = '#define __cpp_lib_test 1L\n' if 'charconv' in source else ''
        return subprocess.CompletedProcess(args, 1 if 'broken' in source or 'tree.hpp' in source else 0, out, '')
    flags = ['-O2', '-DX=1', '-UX', '-march=native']
    with patch('capture.command', side_effect=compiler), patch('capture.header_names',
            return_value=['charconv', 'empty', 'missing', 'broken', 'ext/pb_ds/tree.hpp']):
        path = capture('/fixture/g++', 'c++23', flags, 'fixture', tmp_path / 'capture.json',
                       search=[search], reached={search / 'charconv', search / 'ext/pb_ds/tree.hpp'})
    data = json.loads(path.read_text())
    assert data['headers']['charconv']['macros'] == {'__cpp_lib_test': '1L'}
    assert data['headers']['empty'] == dict(status='ok', macros={})
    assert data['headers']['missing']['status'] == 'missing'
    assert data['headers']['broken']['status'] == 'error'
    assert data['headers']['ext/pb_ds/tree.hpp'] == dict(status='error', macros={})
    assert data['audit']['unparsed'] == {
        '0/ext/pb_ds/tree.hpp': '0/ext/pb_ds/tree.hpp: unterminated conditional'}
    assert data['audit']['macros']['__cpp_lib_test']['independent']
    for args, source_text, _ in calls:
        if '-dM' in args:
            assert args == ['-x', 'c++', '-std=c++23', *flags, '-dM', '-E', '-']
            assert source_text.count('#include') == 1
    assert '__AVX2__' in data['volatile']
    assert '__AVX2__' not in data['audit']['compiler_inputs']
    assert data['audit']['compiler_inputs']['__cplusplus'] == '#define __cplusplus 202302L'
    assert 'ext/pb_ds/tree.hpp' in header_names([search])
    assert dump('#define __glibcxx_a 201703L\n#define __cpp_lib_a __glibcxx_a') == {'__cpp_lib_a': '201703L'}
    assert dump('#define __cpp_lib_a 2L\n#define __GNUC__ 15') == {'__cpp_lib_a': '2L'}
    with pytest.raises(ValueError):
        dump('#define __cpp_lib_a(x) x')


def test_profile_forced_includes_have_no_header_facts(tmp_path):
    def compiler(executable, args, source='', check=True):
        text = '15.2.0' if args[0] == '-dumpfullversion' else 'fixture'
        return subprocess.CompletedProcess(args, 0, text, '')
    with patch('capture.command', side_effect=compiler), patch('capture.header_names', return_value=['charconv']):
        path = capture('/fixture/g++', 'c++23', ['-include', 'forced.h'], 'fixture',
                       tmp_path / 'disabled.json', search=[], reached=set())
    assert json.loads(path.read_text())['headers']['charconv'] == dict(status='disabled', macros={})


def test_diagnostic_probe_separate_tu_no_added_headers_and_mismatch():
    data = report()
    data['audit']['macros']['__cpp_lib_absent'] = {}
    generated = source(data, 'charconv')
    assert generated.count('#include') == 1
    assert '#include <charconv>' in generated
    assert '#ifdef __cpp_lib_absent' in generated
    lines = 'DLIB|charconv|<header>|ok\nDLIB|charconv|__cpp_lib_test|1L\nDLIB|charconv|__cpp_lib_absent|<undefined>\n'
    assert compare(data, 'charconv', lines) == []
    assert '__cpp_lib_test' in compare(data, 'charconv', lines.replace('|1L', '|2L'))[0]
    with pytest.raises(ValueError):
        compare(data, 'charconv', lines + lines)
    assert compare(data, 'charconv', '')
    data['headers']['charconv']['status'] = 'error'
    with pytest.raises(ValueError):
        compare(data, 'charconv', lines)


def test_workflow_wiring_and_actions_pinned():
    workflows = {n: yaml.safe_load((ROOT / f'.github/workflows/{n}.yml').read_text())
                 for n in ['library', 'extensions', 'profiles', 'drift']}
    pipeline = workflows['library']['jobs']
    assert pipeline['profiles']['with']['profiles'] == 'all'
    assert pipeline['profiles']['with']['artifact_prefix'] == 'library-snapshot'
    assert set(pipeline['data']['needs']) == {'profiles', 'extensions'}
    assert any('--check data/library/library.json' in s.get('run', '') for s in pipeline['data']['steps'])
    assert 'library' in workflows['drift']['jobs']
    assert 'library-comparison' in workflows['drift']['jobs']['report']['needs']
    for name, workflow in workflows.items():
        assert workflow['permissions'] == {'contents': 'read'}
        for job in workflow['jobs'].values():
            for step in job.get('steps', []):
                if 'uses' in step:
                    assert '@' in step['uses'] and len(step['uses'].split('@')[1]) == 40
    for job in ['snapshot', 'snapshot-container']:
        upload = next(s for s in workflows['profiles']['jobs'][job]['steps'] if s.get('with', {}).get('name', '').endswith('${{ matrix.profile }}'))
        assert '-library.json' in upload['with']['path']
