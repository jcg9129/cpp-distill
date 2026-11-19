# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic headers and compiler mocks only; never build or execute C++."""
import copy
import json
from pathlib import Path
import random
import subprocess
import sys
from unittest.mock import patch

import pytest
import yaml

from audit import ROOT, VERSION, classify, identifiers, parse, header_test_key, apply_trust
from fixtures import l1c_reference as reference
from capture import capture, dump, header_names, write
from merge import (merge, check, expected_builds, baseline_text, digest,
                   get_snapshot, get_agreement, get_classification)
from probe import compare, source, build_inputs, trusted_build_inputs


def audit(text, extra=None, predefines=()):
    return classify({'0/version': text, **(extra or {})}, predefines, roots=['version'])


def macro(text, **kwargs):
    return audit(text, **kwargs)['roots']['version']['macros']['__cpp_lib_test']


def test_per_root_paths_do_not_inherit_other_roots_conditions():
    files = {
        '0/charconv': '#include <bits/version.h>',
        '0/pb_ds': '#if DEBUG && PB_DS_TRACE\n#include <middle>\n#endif',
        '0/middle': '#include <bits/version.h>',
        '0/bits/version.h': '#if __cplusplus >= 201703L\n#define __cpp_lib_test 1L\n#endif',
    }
    data = classify(files, (), roots=['charconv', 'pb_ds'])
    clean = data['roots']['charconv']['macros']['__cpp_lib_test']
    bad = data['roots']['pb_ds']['macros']['__cpp_lib_test']
    assert clean['independent'] and clean['dependencies'] == ['__cplusplus']
    assert bad['unrecognised'] == ['DEBUG', 'PB_DS_TRACE']
    # An unconditional alternative never erases a conditional path.
    files['0/charconv'] += '\n#include <pb_ds>'
    assert not classify(files, (), roots=['charconv'])['roots']['charconv']['macros']['__cpp_lib_test']['independent']


def test_own_conditions_elif_else_and_each_literal_pair():
    text = ('#if __cplusplus > 202002L\n#define __cpp_lib_test 2L\n'
            '#elif SWITCH\n#define __cpp_lib_test 1L\n#else\n#define __cpp_lib_test 3L\n#endif')
    entry = macro(text)
    assert entry['values'] == ['1L', '2L', '3L']
    assert not entry['independent']
    assert entry['pairs']['2L']['independent']
    assert entry['pairs']['1L']['unrecognised'] == ['SWITCH']
    assert entry['pairs']['3L']['unrecognised'] == ['SWITCH']
    assert macro('#ifndef __cpp_lib_test\n#define __cpp_lib_test 1L\n#endif')['independent']
    assert not macro('#if __cpp_lib_test > 2\n#define __cpp_lib_test 3L\n#endif')['independent']


@pytest.mark.parametrize('dependency', ['_GLIBCXX_USE_CXX11_ABI', '_GLIBCXX_HAS_GTHREADS',
    '_GLIBCXX_HOSTED', '_GLIBCXX_USE_TBB_PAR_BACKEND', '_GLIBCXX_HAVE_LOCALE_H', 'UNKNOWN'])
def test_build_and_unknown_configuration_fail_closed(dependency):
    entry = macro(f'#if {dependency}\n#define __cpp_lib_test 1L\n#endif')
    assert not entry['independent'] and dependency in entry['unrecognised']


@pytest.mark.parametrize('predicate', ['__has_include', '__has_include_next'])
@pytest.mark.parametrize('operand', ['<ext/numeric_traits.h>', '"stacktrace"', 'HEADER(operand)'])
def test_header_test_arguments_are_not_macro_names(predicate, operand):
    expression = f'{predicate}({operand})'
    assert identifiers(expression) == {predicate}
    entry = macro(f'#if {expression}\n#define __cpp_lib_test 1L\n#endif', predefines={predicate})
    assert entry['unrecognised'] == [header_test_key(predicate, operand, '0/version')
                                      if operand.startswith(('<', '"')) else predicate]


def test_literal_include_operands_are_not_macro_names():
    data = classify({'0/root': '#include <ext/numeric_traits.h>',
                     '0/ext/numeric_traits.h': '#define __cpp_lib_test 1L'}, (), roots=['root'])
    assert data['roots']['root']['macros']['__cpp_lib_test']['dependencies'] == []


@pytest.mark.parametrize('broken', ['#else', '#ifdef X', '#undef BAD extra'])
def test_unparsed_and_computed_taint_only_reachable_roots(broken):
    files = {'0/clean': '#define __cpp_lib_test 1L',
             '0/bad': '#include <broken>\n#define __cpp_lib_test 1L',
             '0/broken': broken + '\n#include <leaf>',
             '0/leaf': '#define __cpp_lib_leaf 2L',
             '0/computed': '#include HEADER\n#define __cpp_lib_test 1L'}
    data = classify(files, (), roots=['clean', 'bad', 'computed'])
    assert data['roots']['clean']['macros']['__cpp_lib_test']['independent']
    bad = '<unparsed-file:0/broken>'
    assert data['roots']['bad']['taint'] == [bad]
    for entry in data['roots']['bad']['macros'].values():
        assert bad in entry['unrecognised']
    assert data['roots']['computed']['taint'] == ['<computed-include>']
    assert not data['roots']['computed']['macros']['__cpp_lib_test']['independent']
    assert data['unparsed']['0/broken'].startswith('0/broken: ')


def test_unparsed_feature_tokens_do_not_taint_another_root():
    data = audit('#define __cpp_lib_test 1L', {'0/broken': '#if 1\n#define __cpp_lib_hidden 1L'})
    assert data['roots']['version']['macros']['__cpp_lib_test']['independent']
    assert data['macros']['__cpp_lib_hidden'] == []


@pytest.mark.parametrize('wrapper_guard', ['_GLIBCXX_MATH_H', '_GLIBCXX_STDLIB_H'])
def test_search_order_quotes_and_include_next_wrapper_guards(wrapper_guard):
    files = {
        '0/root': '#include <cmath>',
        '0/cmath': '#ifndef _GLIBCXX_CMATH\n#define _GLIBCXX_CMATH 1\n#include_next <math.h>\n#endif',
        # A common C wrapper guard is nested in an outer else, not whole-file.
        '1/math.h': '#if __cplusplus < 1\n#include_next <math.h>\n#else\n'
                    '#ifndef _GLIBCXX_MATH_H\n#define _GLIBCXX_MATH_H 1\n#include "feature"\n#endif\n#endif',
        '1/feature': '#define __cpp_lib_test 1L',
        '0/feature': '#if WRONG_SEARCH_HIT\n#define __cpp_lib_test 2L\n#endif',
        '0/math.h': '#if WRONG_INCLUDE_NEXT_HIT\n#define __cpp_lib_test 3L\n#endif',
    }
    files['1/math.h'] = files['1/math.h'].replace('_GLIBCXX_MATH_H', wrapper_guard)
    data = classify(files, (), roots=['root'])
    entry = data['roots']['root']['macros']['__cpp_lib_test']
    assert entry['independent'] and entry['values'] == ['1L']
    assert entry['dependencies'] == ['__cplusplus']
    assert data['guards'] == sorted(['_GLIBCXX_CMATH', wrapper_guard])
    assert next(i for i in data['includes'] if i['file'] == '0/cmath')['target'] == '1/math.h'


@pytest.mark.parametrize('outside', ['#define GUARD 2', '#undef GUARD', '#define GUARD 1'])
def test_guard_written_elsewhere_is_not_trusted(outside):
    data = audit('#ifndef GUARD\n#define GUARD 1\n#define __cpp_lib_test 1L\n#endif', {'0/other': outside})
    assert data['roots']['version']['macros']['__cpp_lib_test']['unrecognised'] == ['GUARD']


def test_guards_not_globally_trusted_and_overrides_revoke_guard_trust():
    text = '#ifndef GUARD\n#define GUARD 1\n#endif\n#if GUARD\n#define __cpp_lib_test 1L\n#endif'
    assert macro(text)['unrecognised'] == ['GUARD']
    text = '#ifndef GUARD\n#define GUARD 1\n#define __cpp_lib_test 1L\n#endif'
    assert not macro(text, predefines={'GUARD'})['independent']
    assert not classify({'0/version': text}, (), {'GUARD'}, roots=['version'])['roots']['version']['macros']['__cpp_lib_test']['independent']
    # A later negative test is not the wrapper test, and may reflect SWITCH.
    text = '#if SWITCH\n#ifndef GUARD\n#define GUARD 1\n#endif\n#endif\n#ifndef GUARD\n#define __cpp_lib_test 1L\n#endif'
    assert macro(text)['unrecognised'] == ['GUARD']
    _, _, guards = parse('#ifndef GUARD\n#define GUARD 1\n#else\n#define X 1\n#endif', 'fixture')
    assert not guards


def test_compiler_inputs_and_candidate_writes_are_verified_per_root():
    feature = '#include <config>\n#if _GLIBCXX_FLOAT_IS_IEEE_BINARY32 && __SIZE_WIDTH__\n#define __cpp_lib_test 1L\n#endif'
    config = '#if __FLT_MANT_DIG__ == 24\n#define _GLIBCXX_FLOAT_IS_IEEE_BINARY32 1\n#endif'
    extra = {'0/config': config, '0/other': '#define __SIZE_WIDTH__ 2'}
    entry = macro(feature, extra=extra, predefines={'__FLT_MANT_DIG__', '__SIZE_WIDTH__'})
    assert entry['independent']
    assert not macro(feature, extra={'0/config': config.replace('__FLT_MANT_DIG__', '_GLIBCXX_HOSTED')},
                     predefines={'__SIZE_WIDTH__'})['independent']
    assert not macro(feature, extra={'0/config': config + '\n#undef _GLIBCXX_FLOAT_IS_IEEE_BINARY32'},
                     predefines={'__FLT_MANT_DIG__', '__SIZE_WIDTH__'})['independent']
    assert not macro('#define __SIZE_WIDTH__ 64\n#if __SIZE_WIDTH__\n#define __cpp_lib_test 1L\n#endif',
                     predefines={'__SIZE_WIDTH__'})['independent']


def test_verified_requests_alias_values_and_unwritten_alternatives():
    generated = ('#ifndef __cpp_lib_test\n#if __cplusplus > 0\n#define __glibcxx_test 202100L\n'
                 '#if defined(__glibcxx_want_all) || defined(__glibcxx_want_test)\n'
                 '#define __cpp_lib_test __glibcxx_test\n#endif\n#endif\n#endif\n#undef __glibcxx_want_test')
    extra = {'0/generated': generated, '0/other': '#define __glibcxx_want_all'}
    entry = macro('#define __glibcxx_want_test\n#include <generated>', extra=extra)
    assert entry['independent'] and entry['values'] == ['202100L']
    assert not macro(generated)['independent']  # prefix alone is insufficient
    extra['0/request'] = '#if THREADS\n#define __glibcxx_want_test\n#endif'
    assert 'THREADS' in macro('#include <request>\n#include <generated>', extra=extra)['unrecognised']


@pytest.mark.parametrize('text', ['#define __cpp_lib_test OTHER', '#define __cpp_lib_test(x) x',
                                 '#define __cpp_lib_test 1L\n#undef __cpp_lib_test'])
def test_nonliteral_writes_and_undef_fail_closed(text):
    entry = macro(text)
    assert not entry['independent']
    assert all(not p['independent'] for p in entry['pairs'].values())


def test_comments_raw_strings_continuations_and_malformed_directives():
    text = '/* #define __cpp_lib_fake 1 */\n#ifndef G\n#define G\n#de' + chr(92) + '\nfine __cpp_lib_test 201703L\n#endif'
    assert macro(text)['independent']
    assert not audit('auto x = R"(\n#define __cpp_lib_fake 1L\n)";')['macros']
    for text in ['#else', '#endif', '#if 1', '#define 123', '#undef X extra', '#if 1\n#else\n#elif 1']:
        with pytest.raises(ValueError):
            parse(text, 'fixture')


def report(build='a', value='1L', status='ok', independent=True):
    text = '#define __cpp_lib_test 1L'
    if not independent:
        text = '#if _GLIBCXX_HAS_GTHREADS\n' + text + '\n#endif'
    return dict(schema=2, generator=VERSION, build=build, gcc='14.2.0', major='14',
                headers={'charconv': dict(status=status, macros={} if value is None else {'__cpp_lib_test': value})},
                audit=classify({'0/charconv': text}, (), roots=['charconv']))


def test_agreement_presence_value_absence_and_explicit_unknown():
    data = merge([report('a'), report('b')], ['a', 'b'])
    assert get_agreement(data, '14', 'charconv')['defined'] == {'__cpp_lib_test': '1L'}
    for value in ['2L', None]:
        data = merge([report('a'), report('b', value)], ['a', 'b'])
        assert get_agreement(data, '14', 'charconv')['unknown']['__cpp_lib_test'] == 'cross-build-disagreement'
    data = merge([report('a', None), report('b', None)], ['a', 'b'])
    assert get_agreement(data, '14', 'charconv')['absent'] == ['__cpp_lib_test']
    for status in ['missing', 'error', 'disabled']:
        data = merge([report('a'), report('b', status=status)], ['a', 'b'])
        assert get_agreement(data, '14', 'charconv')['unknown']['__cpp_lib_test'] == 'unavailable-header'
    data = merge([report('a'), report('b', independent=False)], ['a', 'b'])
    assert get_classification(data, '14', 'charconv')['macros']['__cpp_lib_test']['dependencies'] == ['_GLIBCXX_HAS_GTHREADS']
    assert get_agreement(data, '14', 'charconv')['unknown']['__cpp_lib_test'] == 'config-dependent'
    assert get_snapshot(data, 'b', 'charconv')['macros']['__cpp_lib_test'] == '1L'


def test_merger_classifies_per_header_and_absence_needs_complete_root():
    data = report()
    data['headers']['other'] = dict(status='ok', macros={})
    data['audit'] = classify({'0/charconv': '#define __cpp_lib_test 1L',
                              '0/other': '#include COMPUTED'}, (), roots=data['headers'])
    merged = merge([data], ['a'])
    assert get_agreement(merged, '14', 'charconv')['defined']
    assert get_agreement(merged, '14', 'other')['unknown'] == {'__cpp_lib_test': 'config-dependent'}
    data['audit'] = classify({'0/charconv': '#if CONFIG\n#define __cpp_lib_test 1L\n#endif',
                              '0/other': ''}, (), roots=data['headers'])
    merged = merge([data], ['a'])
    assert get_agreement(merged, '14', 'other')['absent'] == ['__cpp_lib_test']


def test_merger_retains_independent_literal_pairs_and_rejects_unaudited_values():
    data = report(value='2L')
    data['audit'] = classify({'0/charconv': '#if __cplusplus > 0\n#define __cpp_lib_test 2L\n'
                             '#elif CONFIG\n#define __cpp_lib_test 1L\n#endif'}, (), roots=['charconv'])
    merged = merge([data], ['a'])
    entry = get_classification(merged, '14', 'charconv')['macros']['__cpp_lib_test']
    assert not entry['independent'] and entry['values'] == ['2L']
    assert get_agreement(merged, '14', 'charconv')['defined'] == {'__cpp_lib_test': '2L'}
    data['headers']['charconv']['macros']['__cpp_lib_test'] = '999L'
    assert not get_agreement(merge([data], ['a']), '14', 'charconv')['defined']


def test_full_matrix_versions_headers_and_schema_required():
    for reports, expected in [([report('a')], ['a', 'b']), ([report('a'), report('a')], ['a']),
                              ([report('a'), report('b')], ['a'])]:
        with pytest.raises(ValueError):
            merge(reports, expected)
    for field in ['schema', 'generator']:
        bad = report(); bad[field] = 1
        with pytest.raises(ValueError):
            merge([bad], ['a'])
    other = report('b'); other.update(major='17', gcc='17.1.0')
    other['headers']['new'] = dict(status='missing', macros={})
    result = merge([report(), other], ['a', 'b'])
    assert set(result['versions']) == {'14', '17'}
    assert get_agreement(result, '17', 'new')['unknown']
    assert any(b.startswith('profile-timus-') for b in expected_builds())
    assert 'extensions-mingw' in expected_builds() and 'extensions-macos-arm64' not in expected_builds()


def test_compact_digest_determinism_and_evidence_only_check(tmp_path, capsys):
    a, b = report('a'), report('b')
    original = merge([a, b], ['a', 'b'])
    text = baseline_text(original)
    assert text == baseline_text(merge([b, a], ['b', 'a']))
    assert json.loads(text) == original
    assert 'audit' not in original['builds']['a']
    assert original['builds']['a']['evidence_sha256'] == digest(a)
    assert len(original['builds']['a']['evidence_sha256']) == 64
    path = tmp_path / 'library.json'; path.write_text(text)
    assert check(text, path)
    changed = copy.deepcopy(a)
    changed['audit']['includes'].append(dict(file='0/unrelated', operand='<new>', conditions=['CONFIG']))
    candidate = merge([changed, b], ['a', 'b'])
    assert candidate['agreement'] == original['agreement']
    assert candidate['versions'] == original['versions']
    assert not check(baseline_text(candidate), path)
    assert 'evidence_sha256' in capsys.readouterr().out
    assert not check('{}\n', tmp_path / 'absent')
    path.write_bytes(b'{}\r\n')
    assert not check(b'{}\n', path)
    assert 'line endings' in capsys.readouterr().out


def test_merger_cli_uploads_full_evidence_even_when_check_fails(tmp_path):
    artifacts = tmp_path / 'input'; artifacts.mkdir()
    reports = []
    for build in expected_builds():
        data = report(build); reports.append(data)
        write(artifacts / (build + '-library.json'), data)
    output = tmp_path / 'out/library.json'
    command = [sys.executable, str(ROOT / 'ci/library/merge.py'), str(artifacts),
               '--output', str(output), '--check', str(tmp_path / 'absent')]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert result.returncode == 1 and 'L1c data changed' in result.stderr
    full = json.loads(output.with_name('library-evidence.json').read_text())
    assert full['builds'] == {r['build']: r for r in reports}
    compact = json.loads(output.read_text())
    for report_ in reports:
        assert digest(full['builds'][report_['build']]) == compact['builds'][report_['build']]['evidence_sha256']
    result = subprocess.run(command[:-1] + [str(output)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr


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
                       search=[search], reached={'0/charconv': search / 'charconv',
                           '0/ext/pb_ds/tree.hpp': search / 'ext/pb_ds/tree.hpp'})
    data = json.loads(path.read_text())
    assert data['headers']['charconv']['macros'] == {'__cpp_lib_test': '1L'}
    assert data['headers']['empty'] == dict(status='ok', macros={})
    assert data['headers']['missing']['status'] == 'missing'
    assert data['headers']['broken']['status'] == 'error'
    assert data['headers']['ext/pb_ds/tree.hpp'] == dict(status='error', macros={})
    assert data['audit']['unparsed'] == {
        '0/ext/pb_ds/tree.hpp': '0/ext/pb_ds/tree.hpp: unterminated conditional'}
    assert data['audit']['roots']['charconv']['macros']['__cpp_lib_test']['independent']
    for args, source_text, _ in calls:
        if '-dM' in args:
            assert args == ['-x', 'c++', '-std=c++23', *flags, '-dM', '-E', '-']
            assert source_text.count('#include') == 1
    assert [source_ for args, source_, _ in calls if '-dM' in args] == [
        f'#include <{h}>\n' for h in ['bits/c++config.h', 'charconv', 'empty', 'broken', 'ext/pb_ds/tree.hpp']]
    assert '__AVX2__' in data['volatile']
    assert '__AVX2__' not in data['audit']['compiler_inputs']
    assert data['audit']['compiler_inputs']['__cplusplus'] == '#define __cplusplus 202302L'
    assert 'ext/pb_ds/tree.hpp' in header_names([search])
    assert dump('#define __glibcxx_a 201703L\n#define __cpp_lib_a __glibcxx_a') == {'__cpp_lib_a': '201703L'}
    assert dump('#define __cpp_lib_a 2L\n#define __GNUC__ 15') == {'__cpp_lib_a': '2L'}
    with pytest.raises(ValueError):
        dump('#define __cpp_lib_a(x) x')


@pytest.mark.parametrize('handoff', [False, True], ids=['profile', 'extensions'])
def test_capture_symlinked_headers_preserves_include_dependencies(tmp_path, handoff):
    from generate import reachable
    search, outside = tmp_path / 'include', tmp_path / 'uapi'
    search.mkdir()
    outside.mkdir()
    try:
        (search / 'asm').symlink_to(outside, target_is_directory=True)
        (search / 'alias').symlink_to(outside, target_is_directory=True)
    except (OSError, NotImplementedError) as error:
        pytest.skip(f'directory symlinks unavailable: {error}')
    (search / 'charconv').write_text('#if _GLIBCXX_HOSTED\n#include <asm/bitsperlong.h>\n#endif\n'
                                   '#if _GLIBCXX_HAS_GTHREADS\n#include <alias/bitsperlong.h>\n#endif\n')
    (outside / 'bitsperlong.h').write_text('#include "local.h"\n')
    (outside / 'local.h').write_text('#define __cpp_lib_test 1L\n')

    def compiler(executable, args, source='', check=True):
        out = '15.2.0' if args[0] == '-dumpfullversion' else 'fixture'
        return subprocess.CompletedProcess(args, 0, out, '')

    kwargs = {'reached': reachable([search], with_spellings=True)} if handoff else {}
    with patch('capture.command', side_effect=compiler), patch('capture.header_names', return_value=['charconv']):
        path = capture('/fixture/g++', 'c++23', [], 'fixture', tmp_path / 'symlink.json',
                       search=[search], **kwargs)
    data = json.loads(path.read_text())['audit']
    entry = data['roots']['charconv']['macros']['__cpp_lib_test']
    assert not entry['independent']
    assert entry['unrecognised'] == ['_GLIBCXX_HAS_GTHREADS', '_GLIBCXX_HOSTED']
    assert {definition['file'] for definition in data['macros']['__cpp_lib_test']} == {
        '0/asm/local.h', '0/alias/local.h'}
    assert {include['file'] for include in data['includes']} == {
        '0/charconv', '0/asm/bitsperlong.h', '0/alias/bitsperlong.h'}
    assert not data['unparsed']


def test_profile_forced_includes_have_no_header_facts(tmp_path):
    def compiler(executable, args, source='', check=True):
        text = '15.2.0' if args[0] == '-dumpfullversion' else 'fixture'
        return subprocess.CompletedProcess(args, 0, text, '')
    with patch('capture.command', side_effect=compiler), patch('capture.header_names', return_value=['charconv']):
        path = capture('/fixture/g++', 'c++23', ['-include', 'forced.h'], 'fixture',
                       tmp_path / 'disabled.json', search=[], reached={})
    assert json.loads(path.read_text())['headers']['charconv'] == dict(status='disabled', macros={})


def test_diagnostic_probe_separate_tu_no_added_headers_and_mismatch():
    data = report()
    data['audit']['macros']['__cpp_lib_absent'] = []
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
    assert pipeline['profiles']['with']['cold_start'] == '${{ inputs.cold_start || false }}'
    assert workflows['library'][True]['workflow_call']['inputs']['cold_start']['default'] is False
    assert workflows['profiles'][True]['workflow_call']['inputs']['cold_start']['default'] is False
    assert workflows['drift']['jobs']['library']['with']['cold_start'] == (
        "${{ github.event_name == 'schedule' && github.event.schedule == '17 6 * * 1' }}")
    assert set(pipeline['data']['needs']) == {'profiles', 'extensions'}
    assert any('--check data/library/library.json' in s.get('run', '') for s in pipeline['data']['steps'])
    upload = next(s for s in pipeline['data']['steps'] if s.get('with', {}).get('name') == 'library-generated')
    assert set(upload['with']['path'].splitlines()) == {'out/library/library.json', 'out/library/library-evidence.json'}
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
    container = workflows['profiles']['jobs']['snapshot-container']
    assert container['timeout-minutes'] == 30
    setup = next(s for s in container['steps'] if s.get('name') == 'Install container tools')
    assert 'time apt-get' in setup['run'] and 'Acquire::http::Timeout=30' in setup['run']
    assert 'ninja-build g++' not in setup['run']
    snapshot = next(s for s in container['steps'] if s.get('name', '').startswith('Snapshot QOJ'))
    assert 'time python3' in snapshot['run']




def test_per_root_all_path_evidence_against_small_graph_oracle():
    for seed in range(40):
        rng = random.Random(seed)
        nodes = list(range(6))
        edges = [(a, b, f'SWITCH_{a}_{b}') for a in nodes for b in nodes if rng.random() < .2]
        files = {f'0/h{a}': f'#define __cpp_lib_f{a} 1L\n' + ''.join(
            f'#if {dep}\n#include <h{b}>\n#endif\n' for source, b, dep in edges if source == a) for a in nodes}
        closure = {a: {a} for a in nodes}
        for _ in nodes:
            for a, b, _ in edges:
                closure[a].update(closure[b])
        result = classify(files, (), roots=[f'h{a}' for a in nodes])
        for root in nodes:
            macros = result['roots'][f'h{root}']['macros']
            assert set(macros) == {f'__cpp_lib_f{a}' for a in closure[root]}
            for target in closure[root]:
                expected = sorted(dep for a, b, dep in edges if a in closure[root] and target in closure[b])
                entry = macros[f'__cpp_lib_f{target}']
                assert entry['dependencies'] == expected, (seed, root, target)
                assert entry['independent'] == (not expected)


def test_request_sccs_match_frozen_reference_for_all_three_node_graphs():
    # The old global include semantics are deliberately not the oracle. Its
    # request/value expansion still supplies an independent single-file oracle.
    names = ['__glibcxx_want_' + str(i) for i in range(3)]
    for mask in range(1 << 9):
        text = ''
        for i, name in enumerate(names):
            deps = [other for j, other in enumerate(names) if mask & (1 << (3*i+j))]
            if deps:
                text += '#if ' + ' && '.join(deps) + '\n'
            text += f'#define {name}\n'
            if deps:
                text += '#endif\n'
            text += f'#if {name}\n#define __cpp_lib_{i} 1L\n#endif\n'
        files = {'0/version': text}
        actual = classify(files, (), roots=['version'])['roots']['version']['macros']
        expected = reference.classify(files, ())['macros']
        for name in actual:
            for field in ('values', 'independent', 'dependencies'):
                assert actual[name][field] == expected[name][field], (mask, name, field)
        reach = {i: {j for j in range(3) if mask & (1 << (3*i+j))} for i in range(3)}
        for _ in range(3):
            for i in range(3):
                reach[i].update(set().union(*(reach[j] for j in list(reach[i]))))
        cyclic = {i for i in range(3) if i in reach[i]}
        for i in range(3):
            bad = sorted(names[j] for j in (reach[i] | {i}) & cyclic)
            assert actual[f'__cpp_lib_{i}']['unrecognised'] == bad


def test_shared_request_and_value_chains_scale_without_losing_evidence():
    text = '#define __glibcxx_want_0\n#define __glibcxx_want_1\n'
    text += '#define __glibcxx_0 202600L\n#define __glibcxx_1 202600L\n'
    for i in range(2, 60):
        text += (f'#if __glibcxx_want_{i-1} && __glibcxx_want_{i-2}\n'
                 f'#define __glibcxx_want_{i}\n#endif\n'
                 f'#define __glibcxx_{i} __glibcxx_{i-1}\n'
                 f'#define __glibcxx_{i} __glibcxx_{i-2}\n')
    for i in range(60):
        text += f'#if __glibcxx_want_{i}\n#define __cpp_lib_{i} __glibcxx_{i}\n#endif\n'
    macros = classify({'0/version': text}, (), roots=['version'])['roots']['version']['macros']
    assert len(macros) == 60
    assert all(e['values'] == ['202600L'] and e['independent'] for e in macros.values())


def test_delta_maps_restore_all_classifications_and_default_taint():
    reports = []
    for i in range(3):
        headers = {f'h{h}': dict(status='ok', macros={}) for h in range(9)}
        files = {f'0/h{h}': '#include <version>\n' + (f'#if SWITCH_{h}\n#include <version>\n#endif' if h else '')
                 for h in range(8)}
        files['0/version'] = ''.join(f'#if BUILTIN_{n}\n#define __cpp_lib_f{n} {n}L\n#endif\n' for n in range(12))
        files['0/h7'] += '\n#include COMPUTED'
        files['0/h8'] = '#include <partial>'
        files['0/partial'] = ''.join(f'#if BUILTIN_{n}\n#define __cpp_lib_f{n} {n}L\n#endif\n' for n in range(11))
        data = report(str(i)); data['headers'] = headers
        data['audit'] = classify(files, {f'BUILTIN_{n}' for n in range(12)}, roots=headers)
        reports.append(data)
    merged = merge(reports, ['0', '1', '2'])
    assert any('base' in s for s in merged['macro_sets'].values())
    for header in reports[0]['headers']:
        decoded = get_classification(merged, '14', header)
        original = reports[0]['audit']['roots'][header]
        assert decoded['default']['dependencies'] == original['taint']
        for name, entry in original['macros'].items():
            restored = decoded['macros'][name]
            assert restored['independent'] == entry['independent']
            assert restored['values'] == sorted(v for v, p in entry['pairs'].items() if p['independent'])
            assert restored['dependencies'] == sorted(set(entry['dependencies']) | set(entry['unrecognised']))


def test_negative_guard_before_entering_another_file_needs_path_proof():
    files = {'0/root': '#ifndef G\n#include <feature>\n#endif',
             '0/feature': '#ifndef G\n#define G 1\n#define __cpp_lib_test 1L\n#endif'}
    result = classify(files, (), roots=['root'])['roots']['root']['macros']['__cpp_lib_test']
    assert result['independent'] and result['dependencies'] == []
    # A sibling may already have entered the guarded file. Retain the guard
    # uncertainty and the sibling's configuration, rather than assuming fresh.
    files['0/root'] = '#if SWITCH\n#include <feature>\n#endif\n#ifndef G\n#include <middle>\n#endif'
    files['0/middle'] = '#include <feature>'
    result = classify(files, (), roots=['root'])['roots']['root']['macros']['__cpp_lib_test']
    assert result['unrecognised'] == ['G', 'SWITCH']


def test_else_and_negated_own_tests_are_not_treated_as_own_ifndef():
    for text in ('#ifndef __cpp_lib_test\n#else\n#define __cpp_lib_test 1L\n#endif',
                 '#if !(!defined(__cpp_lib_test))\n#define __cpp_lib_test 1L\n#endif',
                 '#if !defined(__cpp_lib_test) == 0\n#define __cpp_lib_test 1L\n#endif'):
        assert macro(text)['unrecognised'] == ['__cpp_lib_test']


def test_same_guard_in_distinct_files_and_include_parent_spelling_fail_closed():
    data = classify({'0/root': '#include "sub/../feature"',
                     '0/sub/../feature': '#ifndef G\n#define G 1\n#define __cpp_lib_test 1L\n#endif',
                     '0/another': '#ifndef G\n#define G 1\n#define SOMETHING_ELSE 1\n#endif'}, (), roots=['root'])
    assert data['roots']['root']['macros']['__cpp_lib_test']['unrecognised'] == ['G']


@pytest.fixture
def trust_files():
    # Reduced GCC 15 request/value plumbing, including a return to the macro
    # provider through a feature-conditional include (concepts -> type_traits).
    return {
        '0/charconv': '#include <bit>\n#define __glibcxx_want_to_chars\n#include <bits/version.h>',
        '0/bit': '#include <concepts>\n#if _GLIBCXX_HOSTED || __has_include(<ext/numeric_traits.h>)\n'
                 '#include <ext/numeric_traits.h>\n#endif',
        '0/concepts': '#define __glibcxx_want_concepts\n#include <bits/version.h>\n'
                      '#if defined(__cpp_lib_concepts)\n#include <type_traits>\n#endif',
        '0/type_traits': '#include <bits/version.h>\n#if __glibcxx_type_trait_variable_templates\n'
                         '#include <concepts>\n#endif',
        '0/ext/numeric_traits.h': '#include <type_traits>\n#include <bits/c++config.h>',
        '0/bits/c++config.h': '#if __has_include(<pstl/pstl_config.h>)\n#include <pstl/pstl_config.h>\n#endif',
        '0/pstl/pstl_config.h': '',
        '0/bits/version.h': (
            '#ifndef __cpp_lib_concepts\n#if __cplusplus > 201703L\n#define __glibcxx_concepts 202002L\n'
            '#if defined(__glibcxx_want_concepts)\n#define __cpp_lib_concepts 202002L\n#endif\n#endif\n#endif\n'
            '#undef __glibcxx_want_concepts\n'
            '#ifndef __cpp_lib_type_trait_variable_templates\n#if __cplusplus > 201402L\n'
            '#define __glibcxx_type_trait_variable_templates 201510L\n#endif\n#endif\n'
            '#if __cplusplus > 201402L\n#if defined(__glibcxx_want_to_chars)\n'
            '#define __cpp_lib_to_chars 201611L\n#endif\n#endif\n#undef __glibcxx_want_to_chars'),
        '0/syncstream': '#if _GLIBCXX_USE_CXX11_ABI\n#define __cpp_lib_syncbuf 201803L\n#endif',
        '0/thread': '#if _GLIBCXX_HAS_GTHREADS\n#define __cpp_lib_jthread 201911L\n#endif',
    }


def trust_report(files, build='a', hosted='1', present=True):
    audit_ = classify(files, (), roots=['charconv', 'syncstream', 'thread'])
    data = dict(schema=2, generator=VERSION, build=build, major='15', gcc='15.2.0',
                headers={'charconv': dict(status='ok', macros={'__cpp_lib_to_chars': '201611L'})},
                audit=audit_, observations=dict(hosted=hosted,
                    headers={k: present for k in audit_['header_tests']}))
    apply_trust(audit_, trusted_build_inputs(data, [data]))
    return data


def test_gcc15_to_chars_trust_fixture_and_config_negatives(trust_files):
    a, b = trust_report(trust_files), trust_report(trust_files, 'b')
    merged = merge([a, b], ['a', 'b'])
    assert get_agreement(merged, '15', 'charconv')['defined'] == {'__cpp_lib_to_chars': '201611L'}
    root = a['audit']['roots']['charconv']
    assert root['macros']['__cpp_lib_to_chars']['independent']
    for name in ['__cpp_lib_concepts', '__glibcxx_type_trait_variable_templates']:
        assert root['trusted_inputs'][name] == dict(rule='independent-library-macro', root='charconv')
    assert root['trusted_inputs']['_GLIBCXX_HOSTED']['rule'] == 'all-major-builds-hosted'
    assert root['trusted_inputs']['_GLIBCXX_HOSTED']['builds'] == ['a', 'b']
    for header, name, config in [('syncstream', '__cpp_lib_syncbuf', '_GLIBCXX_USE_CXX11_ABI'),
                                 ('thread', '__cpp_lib_jthread', '_GLIBCXX_HAS_GTHREADS')]:
        entry = a['audit']['roots'][header]['macros'][name]
        assert not entry['independent'] and config in entry['unrecognised']


@pytest.mark.parametrize('hosted', ['0', None])
def test_nonhosted_or_missing_build_revokes_hosted_trust(trust_files, hosted):
    a, b = trust_report(trust_files), trust_report(trust_files, 'b', hosted=hosted)
    assert a['audit']['roots']['charconv']['macros']['__cpp_lib_to_chars']['independent']
    merged = merge([a, b], ['a', 'b'])
    assert get_agreement(merged, '15', 'charconv')['unknown']['__cpp_lib_to_chars'] == 'config-dependent'
    assert '_GLIBCXX_HOSTED' not in a['audit']['trusted_inputs']
    assert '_GLIBCXX_HOSTED' in a['audit']['roots']['charconv']['macros']['__cpp_lib_to_chars']['unrecognised']


@pytest.mark.parametrize('predicate', ['__has_include', '__has_include_next'])
@pytest.mark.parametrize('present', [True, False])
def test_header_presence_requires_all_builds_including_negative_answers(predicate, present):
    files = {'0/charconv': f'#if {predicate}(<optional.h>)\n#define __cpp_lib_to_chars 201611L\n#endif'}
    a, b = trust_report(files, present=present), trust_report(files, 'b', present=present)
    merged = merge([a, b], ['a', 'b'])
    assert get_agreement(merged, '15', 'charconv')['defined']
    key = next(iter(a['audit']['header_tests']))
    assert a['audit']['trusted_inputs'][key]['value'] is present
    for other in [not present, None]:
        b['observations']['headers'][key] = other
        merged = merge([a, b], ['a', 'b'])
        assert not get_agreement(merged, '15', 'charconv')['defined']
        assert key not in a['audit']['trusted_inputs']
    del b['observations']
    assert not get_agreement(merge([a, b], ['a', 'b']), '15', 'charconv')['defined']


def test_reproduced_profile_header_scope_and_major_isolation():
    files = {'0/charconv': '#if __has_include(<optional.h>)\n#define __cpp_lib_to_chars 201611L\n#endif'}
    a, b = trust_report(files), trust_report(files, 'b', present=False)
    a['reproduced'] = True
    merge([a, b], ['a', 'b'])
    key = next(iter(a['audit']['header_tests']))
    assert a['audit']['trusted_inputs'][key]['builds'] == ['a']
    assert key not in b['audit']['trusted_inputs']
    b['major'] = '14'
    a['reproduced'] = False
    merge([a, b], ['a', 'b'])
    assert a['audit']['trusted_inputs'][key]['builds'] == ['a']
    assert b['audit']['trusted_inputs'][key]['builds'] == ['b']


def test_dependent_library_macro_and_other_root_never_grant_trust():
    files = {'0/charconv': '#include <base>\n#if defined(__cpp_lib_base)\n'
                          '#define __cpp_lib_to_chars 201611L\n#endif',
             '0/base': '#if _GLIBCXX_HAS_GTHREADS\n#define __cpp_lib_base 1L\n#endif',
             '0/other': '#define __cpp_lib_unrelated 1L'}
    data = trust_report(files)
    entry = data['audit']['roots']['charconv']['macros']['__cpp_lib_to_chars']
    assert not entry['independent'] and entry['unrecognised'] == ['_GLIBCXX_HAS_GTHREADS']
    assert '__cpp_lib_base' not in data['audit']['roots']['charconv']['trusted_inputs']
    files['0/charconv'] = files['0/charconv'].replace('__cpp_lib_base', '__cpp_lib_unrelated')
    entry = trust_report(files)['audit']['roots']['charconv']['macros']['__cpp_lib_to_chars']
    assert entry['unrecognised'] == ['__cpp_lib_unrelated']


def test_library_condition_fixpoint_and_unanchored_cycles():
    text = '#define __cpp_lib_base 1L\n'
    for i in range(80):
        previous = '__cpp_lib_base' if not i else f'__glibcxx_link_{i-1}'
        text += f'#if {previous}\n#define __glibcxx_link_{i} 1L\n#endif\n'
    text += '#if __glibcxx_link_79\n#define __cpp_lib_test 1L\n#endif'
    assert macro(text)['independent']
    for cycle in ['#if __cpp_lib_base\n#define __cpp_lib_base 1L\n#endif',
                  '#if __glibcxx_b\n#define __cpp_lib_base 1L\n#endif\n'
                  '#if __cpp_lib_base\n#define __glibcxx_b 1L\n#endif']:
        entry = macro(cycle + '\n#if __cpp_lib_base\n#define __cpp_lib_test 1L\n#endif')
        assert not entry['independent'] and '__cpp_lib_base' in entry['unrecognised']


def test_build_input_probes_batch_contexts_hosted_alias_and_fail_closed(tmp_path):
    search = [tmp_path / 'first', tmp_path / 'second', tmp_path / 'third']
    files = {'1/sub/header': '#if __has_include_next(<h>) && __has_include("local.h")\n'
                            '#define __cpp_lib_test 1L\n#endif'}
    audit_ = classify(files, (), roots=['sub/header'])
    calls = []
    def compiler(executable, args, source='', check=True):
        calls.append((args, source))
        output = ('#define _GLIBCXX_HOSTED __STDC_HOSTED__\n#define __STDC_HOSTED__ 1\n' if '-dM' in args
                  else 'DLIB_INPUT_0 1\n')
        return subprocess.CompletedProcess(args, 0, output, '')
    result = build_inputs('g++', 'c++23', ['-O2'], search, audit_, compiler)
    assert result['hosted'] == '1' and set(result['headers'].values()) == {True}
    assert len(calls) == 3
    next_args = next(a for a, s in calls if '__has_include(<h>)' in s)
    assert next_args == ['-x', 'c++', '-std=c++23', '-nostdinc', '-I-', '-I', str(search[2]), '-E', '-P', '-']
    quote_args = next(a for a, s in calls if '__has_include("local.h")' in s)
    assert quote_args[quote_args.index('-iquote') + 1] == str(search[1] / 'sub')
    def failed(*args, **kwargs):
        return subprocess.CompletedProcess([], 1, '', 'fixture error')
    result = build_inputs('g++', 'c++23', [], search, audit_, failed)
    assert result['hosted'] is None and set(result['headers'].values()) == {None}


def test_header_probe_conflicting_contexts_missing_records_and_overrides(tmp_path):
    files = {f'{i}/header': '#if __has_include_next("local.h")\n#define __cpp_lib_test 1L\n#endif'
             for i in range(2)}
    audit_ = classify(files, (), roots=['header'])
    search = [tmp_path / 'first', tmp_path / 'second']
    def compiler(executable, args, source='', check=True):
        if '-dM' in args:
            output = '#define _GLIBCXX_HOSTED 1\n'
        else:
            # include_next ignores the source's local quote directory. Two
            # spellings of one query must both agree before evidence is usable.
            assert '-iquote' not in args
            output = f'DLIB_INPUT_0 {int("-I" in args)}\n'
        return subprocess.CompletedProcess(args, 0, output, '')
    observed = build_inputs('g++', 'c++23', [], search, audit_, compiler)
    assert observed['hosted'] == '1' and set(observed['headers'].values()) == {None}
    audit_['blocked_inputs'] = ['_GLIBCXX_HOSTED', '__has_include_next']
    observed = build_inputs('g++', 'c++23', [], search, audit_, compiler)
    assert observed['hosted'] is None and set(observed['headers'].values()) == {None}
    audit_['blocked_inputs'] = []
    def missing(*args, **kwargs):
        return subprocess.CompletedProcess([], 0, '', '')
    observed = build_inputs('g++', 'c++23', [], search, audit_, missing)
    assert observed['hosted'] is None and set(observed['headers'].values()) == {None}


def test_absolute_includes_are_explicit_unknown_not_proven_absence():
    result = classify({'0/root': '#include "/not-a-search-relative-name"\n#define __cpp_lib_test 1L'}, (), roots=['root'])
    assert result['roots']['root']['taint'] == ['<absolute-include>']


def test_fourteen_build_compact_size_and_classifications_survive_sharing():
    reports = []
    names = [f'__cpp_lib_feature_{i}' for i in range(242)]
    headers = [f'ext/pb_ds/detail/header_{h}.hpp' for h in range(397)]
    for i in range(14):
        major = str(10 + i//2)
        values = {n: f'{202000 + 100*int(major) + j}L' for j, n in enumerate(names)}
        roots = {}
        for h, header in enumerate(headers):
            deps = [f'CONFIG_{h % 20}'] if h else ['__cplusplus']
            macros = {n: dict(independent=not h, values=[v], dependencies=deps,
                              unrecognised=deps if h else [],
                              requirements=deps if h else [],
                              pairs={v: dict(independent=not h, requirements=deps if h else [])})
                      for n, v in values.items()}
            roots[header] = dict(macros=macros, taint=[])
        reports.append(dict(schema=2, generator=VERSION, build=f'build-{i}', major=major,
                            headers={h: dict(status='ok', macros=values) for h in headers},
                            audit=dict(generator=VERSION, macros=dict.fromkeys(names, []), roots=roots)))
    data = merge(reports, [r['build'] for r in reports])
    assert len(baseline_text(data).encode()) < 2_000_000
    for report_ in reports:
        for header in (headers[0], headers[-1]):
            assert get_snapshot(data, report_['build'], header) == report_['headers'][header]
            restored = get_classification(data, report_['major'], header)['macros']
            for name, entry in report_['audit']['roots'][header]['macros'].items():
                assert restored[name]['independent'] == entry['independent']
                assert restored[name]['dependencies'] == entry['dependencies']


@pytest.mark.parametrize('generated', [False, True], ids=['gcc13-distributed', 'gcc14-generated'])
def test_charconv_compiler_only_conditions_survive_unrelated_pbds_paths(generated):
    condition = ('__cplusplus >= 201703L && _GLIBCXX_FLOAT_IS_IEEE_BINARY32 && '
                 '_GLIBCXX_DOUBLE_IS_IEEE_BINARY64 && __SIZE_WIDTH__ >= 32')
    feature = f'#if {condition}\n#define __cpp_lib_to_chars 201611L\n#endif'
    files = {
        '0/charconv': '#include <bits/c++config.h>\n',
        '0/bits/c++config.h': '#ifndef CONFIG_GUARD\n#define CONFIG_GUARD 1\n'
                             '#if __FLT_MANT_DIG__ == 24\n#define _GLIBCXX_FLOAT_IS_IEEE_BINARY32 1\n#endif\n'
                             '#if __DBL_MANT_DIG__ == 53\n#define _GLIBCXX_DOUBLE_IS_IEEE_BINARY64 1\n#endif\n#endif',
        '0/pb_ds': '#if PB_DS_TREE_TRACE && _GLIBCXX_DEBUG\n#include <charconv>\n#endif',
    }
    if generated:
        files['0/charconv'] += '#define __glibcxx_want_to_chars\n#include <bits/version.h>'
        files['0/bits/version.h'] = ('#ifndef __cpp_lib_to_chars\n'
            f'#if {condition}\n#define __glibcxx_to_chars 201611L\n'
            '#if defined(__glibcxx_want_all) || defined(__glibcxx_want_to_chars)\n'
            '#define __cpp_lib_to_chars __glibcxx_to_chars\n#endif\n#endif\n#endif\n#undef __glibcxx_want_to_chars')
        files['0/version'] = '#define __glibcxx_want_all\n#include <bits/version.h>'
    else:
        files['0/charconv'] += feature
    data = classify(files, {'__FLT_MANT_DIG__', '__DBL_MANT_DIG__', '__SIZE_WIDTH__'}, roots=['charconv', 'pb_ds'])
    clean = data['roots']['charconv']['macros']['__cpp_lib_to_chars']
    assert clean['independent'] and clean['values'] == ['201611L']
    assert not data['roots']['pb_ds']['macros']['__cpp_lib_to_chars']['independent']
    assert 'PB_DS_TREE_TRACE' not in clean['dependencies']


def test_request_replacements_in_another_root_do_not_override_local_evidence():
    files = {'0/root': '#define __glibcxx_want_test\n#if __glibcxx_want_test\n#define __cpp_lib_test 1L\n#endif',
             '0/other': '#define __glibcxx_want_test BAD'}
    assert classify(files, (), roots=['root'])['roots']['root']['macros']['__cpp_lib_test']['independent']
    files['0/root'] = '#include <other>\n' + files['0/root']
    assert not classify(files, (), roots=['root'])['roots']['root']['macros']['__cpp_lib_test']['independent']


def test_internal_own_ifndef_never_erases_inbound_feature_conditions():
    files = {'0/root': '#if __cpp_lib_test\n#include <internal>\n#endif\n#include <feature>',
             '0/internal': '#define __glibcxx_test 1L',
             '0/feature': '#define __cpp_lib_test __glibcxx_test'}
    entry = classify(files, (), roots=['root'])['roots']['root']['macros']['__cpp_lib_test']
    assert not entry['independent']
    assert '__cpp_lib_test' in entry['unrecognised']
