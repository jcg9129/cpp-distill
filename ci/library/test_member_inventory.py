# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import json

import pytest

from embed import generate
from member_inventory import inventory, tokens, validate_inventory
from merge import baseline_text, digest, inventory_bytes, merge
from test_embed import report


def test_raw_identifiers_and_normalization():
    text = r'''
// hidden_line ##
/* hidden_block %:%: */
"hidden_string ##" L"wide_string" u8"utf_string"_literal_suffix
'c' L'x' R"tag(raw_hidden " /* ## %:%: )tag"
0xnumber_hidden 1e+number_hidden 42'123_number_hidden .5_suffix_hidden
#if 0
void inactive_member();
#define MACRO(arg) arg replacement_member
#endif
int caf\u00e9; int café; int \U000003B1; int \u{3b1};
int \N{GREEK SMALL LETTER ALPHA};
int spl\
iced;
'''
    names, pasting = tokens(text)
    assert names == {'if', 'void', 'inactive_member', 'define', 'MACRO', 'arg',
                     'replacement_member', 'endif', 'int', 'café', 'α', 'spliced'}
    assert not pasting


@pytest.mark.parametrize('operator', ['##', '%:%:', '#\\\n#', '%:\\\n%:'])
def test_pasting_report_only_in_raw_tokens(operator):
    names, pasting = tokens('#if 0\n#define CAT(a,b) a ' + operator + ' b\n#endif')
    assert pasting and {'CAT', 'a', 'b'} <= names


def test_inventory_all_reached_headers_including_libc_and_aliases():
    files = {'1/libc.h': 'int libc_member;', '0/root': '#if 0\n#define CAT(x,y) x ## y\n#endif',
             '2/alias': 'int libc_member;'}
    result = inventory(files)
    assert result == inventory(dict(reversed(list(files.items()))))
    assert result['names'] == ['CAT', 'define', 'endif', 'if', 'int', 'libc_member', 'x', 'y']
    assert result['token_pasting'] and result['token_pasting_headers'] == ['0/root']
    assert result['reachable_headers'] == 3 and validate_inventory(result)


def test_unicode_separators_do_not_join_identifiers_or_numbers():
    names, _ = tokens('left\u00a0right α≤β 12\u00a0after_number int a\u0301;')
    assert names == {'left', 'right', 'α', 'β', 'after_number', 'int', 'a\u0301'}


def test_physical_newlines_before_splicing_and_line_comments():
    names, pasting = tokens('// hidden\rint member;\r\nint spl\\\n\riced;\n')
    assert names == {'int', 'member', 'spliced'} and not pasting


def test_raw_literal_body_reverts_line_splicing():
    names, pasting = tokens('R\\\n"tag(## )ta\\\ng" hidden ## )tag"; int visible;')
    assert names == {'int', 'visible'} and not pasting


def test_global_union_determinism_no_repeated_build_sets_and_pasting():
    a, b = report('profile-a'), report('extensions-b', reproduced=False)
    a['member_inventory'] = inventory({'0/a': 'void only_a(); void shared();'})
    b['member_inventory'] = inventory({'0/b': '#define CAT(x,y) x %:%: y\nvoid only_b(); void shared();'})
    data = merge([a, b], [a['build'], b['build']])
    assert data['member_names'] == sorted(set(a['member_inventory']['names'] + b['member_inventory']['names']))
    assert baseline_text(data) == baseline_text(merge([b, a], [b['build'], a['build']]))
    assert 'names' not in data['builds']['profile-a']['member_inventory']
    assert data['builds']['profile-a']['member_inventory']['names_sha256'] == digest(a['member_inventory']['names'])
    assert not data['builds']['profile-a']['member_inventory']['token_pasting']
    assert data['builds']['extensions-b']['member_inventory']['token_pasting']
    embedded = generate(baseline_text(data).encode(), {'charconv'})
    assert 'memberChunks[]' in embedded
    assert f'worker headers + 2 scanned builds (generator {data["generator"]})' in embedded
    assert 'token_pasting_headers' not in embedded


def test_new_captures_cannot_silently_omit_inventory():
    row = report()
    del row['member_inventory']
    with pytest.raises(ValueError, match='inventory'):
        merge([row], [row['build']])


@pytest.mark.parametrize('bad', [None, ['z', 'a'], ['same', 'same'], ['a\\u0062'], ['with space'], [3]])
def test_embed_rejects_malformed_inventory(bad):
    row = report()
    data = merge([row], [row['build']])
    data['member_names'] = bad
    with pytest.raises(ValueError, match='member names'):
        generate(json.dumps(data).encode(), {'charconv'})


def test_embed_missing_inventory_only_supported_for_old_baseline():
    row = report()
    data = merge([row], [row['build']])
    del data['member_names']
    with pytest.raises(ValueError, match='member names'):
        generate(json.dumps(data).encode(), {'charconv'})
    data['generator'] = 3
    for build in data['builds'].values():
        build['generator'] = 3
        del build['member_inventory']
    text = generate(baseline_text(data).encode(), {'charconv'})
    assert 'scanned-build inventory unavailable' in text
    assert 'memberChunks[]' not in text


def test_inventory_changes_evidence_digest_even_when_union_unchanged():
    a, b = report('profile-a'), report('profile-b')
    a['member_inventory'] = inventory({'0/a': 'int shared; int extra;'})
    b['member_inventory'] = inventory({'0/b': 'int shared;'})
    old = merge([copy.deepcopy(a), copy.deepcopy(b)], [a['build'], b['build']])
    b['member_inventory'] = inventory({'0/b': 'int shared; int extra;'})
    new = merge([a, b], [a['build'], b['build']])
    assert old['member_names'] == new['member_names']
    assert old['builds']['profile-b']['evidence_sha256'] != new['builds']['profile-b']['evidence_sha256']


def test_unicode_inventory_embeds_utf8_without_cpp_surrogate_escapes():
    row = report()
    row['member_inventory'] = inventory({'0/header': r'int caf\u00e9; int \U00010400;'})
    data = merge([row], [row['build']])
    text = generate(baseline_text(data).encode(), {'charconv'})
    assert 'café' in text and '𐐀' in text
    assert '\\ud801' not in text


def test_large_inventory_is_stored_once_and_size_is_reported():
    rows = [report('profile-' + str(i)) for i in range(12)]
    names = ['member_' + str(i).zfill(5) for i in range(10_000)]
    for row in rows:
        row['member_inventory'] = dict(names=names, reachable_headers=50,
                                       token_pasting=False, token_pasting_headers=[])
    data = merge(rows, [r['build'] for r in rows])
    size = inventory_bytes(data)
    assert 150_000 < size < 155_000  # 10K names once, plus twelve small summaries.
    assert baseline_text(data).count('"member_00000"') == 1
