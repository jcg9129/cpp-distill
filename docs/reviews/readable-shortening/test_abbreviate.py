"""Tests for abbreviation rules and semantic boundaries of this estimate."""
import unittest

from abbreviate import (ABBREVIATIONS, PROTOCOLS, baseline, candidates,
                        collapse_spaces, mapping_for, parts, word_abbreviation)


class ReadableTests(unittest.TestCase):
    def mapping(self, source, kept=(), predefined=()):
        tokens = baseline.lex(source)
        declared = {t.text for t in tokens if t.kind == 'id'}
        return mapping_for(tokens, declared, set(kept), predefined)[0]

    def test_requested_examples(self):
        self.assertEqual(next(candidates('read_stream_magnitude')), 'rd_strm_mgnt')
        self.assertEqual(next(candidates('isUnsignedIntegral')), 'isUnsgIntg')
        for word, new in [('magnitude', 'mgnt'), ('stream', 'strm'), ('unsigned', 'unsg')]:
            self.assertEqual(word_abbreviation(word), new)

    def test_case_underscores_digits(self):
        self.assertEqual(parts('_readXMLStream2_magnitude_'),
                         ['_', 'read', 'XML', 'Stream', '2', '_', 'magnitude', '_'])
        self.assertEqual(next(candidates('_readXMLStream2_magnitude_')), '_rdXMLStrm2_mgnt_')
        self.assertEqual(next(candidates('READ_STREAM_MAGNITUDE')), 'RD_STRM_MGNT')

    def test_short_words_and_identifiers(self):
        self.assertEqual(self.mapping('read node abcd abc i x_1;'), {})
        self.assertEqual(next(candidates('getLongNumber')), 'getLngNum')
        self.assertEqual(next(candidates('a_b_c')), 'a_b_c')

    def test_dictionary_and_keyword_collision(self):
        self.assertEqual(len(ABBREVIATIONS), 64)
        mapping = self.mapping('integer count_count;')
        self.assertEqual(mapping['integer'], 'inte')
        self.assertNotIn('int', mapping.values())

    def test_block_kept_library_predefined_and_old_names(self):
        mapping = self.mapping('read_stream rd_strm rd_strea rd_stre;', {'rd_strm'}, {'rd_stream'})
        self.assertNotIn('rd_strm', mapping.values())
        self.assertNotIn('rd_stre', mapping.values())
        self.assertNotIn('rd_stream', mapping.values())
        self.assertEqual(mapping['read_stream'], 'rea_strm')
        self.assertEqual(self.mapping('named_value;', predefined={'nmd_val'})['named_value'], 'name_val')
        self.assertEqual(self.mapping('named_value;', kept={'nmd_val'})['named_value'], 'name_val')

    def test_collision_order_and_injectivity(self):
        source = 'comparator comparator compare cmp;'
        mapping = self.mapping(source)
        # cmp is already present; comparator wins the first readable expansion.
        self.assertEqual(mapping['comparator'], 'comp')
        self.assertEqual(mapping['compare'], 'compa')
        self.assertEqual(mapping, self.mapping('cmp compare comparator comparator;'))
        self.assertEqual(len(set(mapping.values())), len(mapping))

    def test_single_letter_lengthening_and_suffix(self):
        names = list(zip(range(10), candidates('vertices_count')))
        lengths = [len(name) for _, name in names]
        self.assertTrue(all(b == a + 1 for a, b in zip(lengths, lengths[1:8])))
        self.assertIn('vertices_count', [name for _, name in names])
        self.assertIn('vertices_count1', [name for _, name in names])

    def test_protocol_names_always_stay(self):
        self.assertEqual(self.mapping(' '.join(sorted(PROTOCOLS))), {})
        self.assertTrue({'promise_type', 'await_transform', 'return_value',
                         'tuple_size', 'tuple_element', 'begin', 'end', 'get'} <= PROTOCOLS)

    def test_reuses_all_baseline_exclusions(self):
        source = ('#define STRINGIFY(arg) #arg protected_name\n'
                  '#define PASTE(a,b) a ## b pasted_name\n'
                  '[[custom::attribute]] int named_value; auto s="named_value"; '
                  'auto t="encoded\\137name"; auto u="x"_suffix; int __reserved; int _Upper;')
        tokens = baseline.lex(source)
        kept, _, _ = baseline.exclusions(tokens, source, {'library_name'}, {'adl_name'})
        self.assertTrue({'STRINGIFY', 'arg', 'protected_name', 'PASTE', 'a', 'b',
                         'pasted_name', 'custom', 'attribute', 'named_value',
                         'encoded_name', '_suffix', '__reserved', '_Upper',
                         'library_name', 'adl_name', 'main'} <= kept)
        self.assertEqual(self.mapping(' '.join(sorted(kept)), kept), {})

    def test_spaces_preserve_literals_comments_headers_and_lines(self):
        source = ('#define OBJ  (1)\n#define FUN(x)  x  +  1\\\n  +  2\n'
                  '#include "a  b.h"\n'
                  'auto  s = "a  b"; auto  r=R"tag(a  b\n c  d)tag"; // a  b\n'
                  '/* a  b */\t int  x;\n')
        collapsed = collapse_spaces(source)
        self.assertEqual(baseline.signature(baseline.lex(source)),
                         baseline.signature(baseline.lex(collapsed)))
        self.assertIn('OBJ (1)', collapsed)
        self.assertIn('FUN(x) x + 1\\\n + 2', collapsed)
        for literal in ['"a  b.h"', '"a  b"', 'R"tag(a  b\n c  d)tag"', '// a  b', '/* a  b */\t']:
            self.assertIn(literal, collapsed)
        self.assertEqual(source.count('\n'), collapsed.count('\n'))
        self.assertEqual(collapse_spaces(collapsed), collapsed)

    def test_cleanup_is_original_line_bytes_and_mapping_is_reused(self):
        source = '#include <vector>\n#include <vector>\n#define DISTILL_ONCE_x\nint named_value;\n'
        tokens = baseline.lex(source)
        clean, inc, once = baseline.cleanup(source, tokens)
        self.assertEqual(len(source) - len(clean), inc + once)
        self.assertEqual(inc, len('#include <vector>\n'))
        self.assertEqual(once, len('#define DISTILL_ONCE_x\n'))
        mapping = self.mapping('named_value;')
        renamed = baseline.render(source, tokens, mapping)
        renamed_clean = baseline.render(clean, baseline.lex(clean), mapping)
        self.assertEqual(len(renamed) - len(renamed_clean), inc + once)


if __name__ == '__main__':
    unittest.main()
