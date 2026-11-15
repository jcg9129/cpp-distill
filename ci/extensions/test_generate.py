# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import tempfile
import unittest
from generate import collect, generate, read_list, reserved, standard


class GeneratorTests(unittest.TestCase):
    def test_directives_and_inactive_branches(self):
        self.assertEqual(collect('#if 0\n # define M_PI 3\n#define F(x) x\n#undef OLD\n#endif\n'), {'M_PI', 'F', 'OLD'})

    def test_splices_before_comments_and_split_names(self):
        self.assertEqual(collect('#de\\\nfine SPL\\\nIT 1\n// hidden \\\n#define HIDDEN 1\n#define V \\\n  2\n'), {'SPLIT', 'V'})
        self.assertEqual(collect('#define A \\ \r\n 1\r\n#undef B\r\n'), {'A', 'B'})

    def test_comments_and_quotes(self):
        text = '/*\n#define NO 1\n*/\n#/**/define/**/YES 1\n#define STR "/*"\n#define NEXT 1 // */\n'
        self.assertEqual(collect(text), {'YES', 'STR', 'NEXT'})
        self.assertEqual(collect('#define/**/NAME 1\n#defineNAME 2\n'), {'NAME'})

    def test_multiline_comments_in_directives_and_raw_strings(self):
        text = '#define /* split\ncomment */ LONG_NAME 1\n'
        text += 'auto s = R"delim(\n" /* not a comment\n)delim";\n#define AFTER_RAW 1\n'
        self.assertEqual(collect(text), {'LONG_NAME', 'AFTER_RAW'})

    def test_reserved_and_standard_families(self):
        for name in ['__X', '_X', 'a__b']:
            self.assertTrue(reserved(name))
        for name in ['_lower', 'GUARD']:
            self.assertFalse(reserved(name))
        for name in ['INT24_MAX', 'UINT_FAST1024_C', 'PRIuLEAST256', 'SCNx24', 'assert']:
            self.assertTrue(standard(name, {'assert'}))
        self.assertFalse(standard('INTbanana_MAX', set()))

    def test_union_exclusion_sorting_and_provenance(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            root = Path(temp)
            inc = root / 'include'
            inc.mkdir()
            (inc / 'c++').mkdir()
            (inc / 'c++' / 'skip.h').write_text('#define SKIP 1\n')
            (inc / 'a.h').write_text('#define Z 1\n#undef A\n#define __INTERNAL 2\n#define assert(x) x\n#define PRIu24 "u"\n')
            (inc / 'extensionless').write_text('#define B(x) x\n')
            output = root / 'list.txt'
            output.write_text('# Scanned: old version\nOLD\n')
            count = generate('glibc', [inc], output, ['fixture v1'], ['c++'])
            self.assertEqual(count, 4)
            self.assertEqual(read_list(output)[0], {'A', 'B', 'OLD', 'Z'})
            self.assertIn('# Scanned: old version', output.read_text())
            self.assertEqual([s for s in output.read_text().splitlines() if not s.startswith('#')], ['A', 'B', 'OLD', 'Z'])
            generate('glibc', [inc], output, ['fixture v2'], ['c++'])
            self.assertIn('fixture v1', output.read_text())
            self.assertIn('fixture v2', output.read_text())

    def test_missing_headers_and_invalid_existing_list(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            root = Path(temp)
            with self.assertRaises(ValueError):
                generate('glibc', [root / 'missing'], root / 'out', ['missing'])
            with self.assertRaises(ValueError):
                generate('glibc', [root], root / 'out', ['empty'])
            (root / 'bad').write_text('NOT A NAME\n')
            with self.assertRaises(ValueError):
                read_list(root / 'bad')


if __name__ == '__main__':
    unittest.main()
