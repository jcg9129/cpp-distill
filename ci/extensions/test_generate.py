# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import tempfile
import unittest
from generate import collect, generate, read_list, reserved, standard, reachable
from scan import attribute, search_paths
from merge import merge
from unittest.mock import patch


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

    def test_reachability_inactive_transitive_relative_include_next_and_pbds(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            root = Path(temp).resolve(); first = root / 'first'; second = root / 'second'
            first.mkdir(); second.mkdir(); (first / 'ext/pb_ds/detail').mkdir(parents=True)
            seeds = root / 'headers.txt'; seeds.write_text('vector\n')
            (first / 'vector').write_text('#if 0\n#include "relative.h"\n#endif\n#include <same.h>\n#include COMPUTED\n')
            (first / 'relative.h').write_text('#define REACH 1\n#include <cycle.h>\n')
            (first / 'cycle.h').write_text('#include <vector>\n')
            (first / 'same.h').write_text('#include_next <same.h>\n')
            (second / 'same.h').write_text('#define NEXT 1\n')
            (first / 'unrelated.h').write_text('#define MOD 1\n#define INF 1\n')
            (first / 'computed.h').write_text('#define COMPUTED_ONLY 1\n')
            (first / 'ext/pb_ds/detail/a.hpp').write_text('#define PBDS 1\n')
            files = reachable([first, second], seeds)
            self.assertEqual({p.name for p in files}, {'vector', 'relative.h', 'cycle.h', 'same.h', 'a.hpp'})
            self.assertIn(second / 'same.h', files)
            output = root / 'list.txt'; output.write_text('OLD_BROAD_SCAN\n')
            self.assertEqual(generate('glibc', files, output, ['fixture v2']), 3)
            self.assertEqual(read_list(output)[0], {'NEXT', 'PBDS', 'REACH'})
            self.assertIn('computed include operands are not followed', output.read_text())

    def test_attribution_and_search_order(self):
        self.assertEqual(attribute(Path('/usr/include/c++/15/vector'), 'linux', []), 'libstdc++')
        self.assertEqual(attribute(Path('/usr/include/stdio.h'), 'linux', [Path('/usr/include')]), 'glibc')
        self.assertIsNone(attribute(Path('/usr/lib/gcc/x/15/include/x.h'), 'linux', [Path('/usr/lib/gcc/x/15/include')]))
        self.assertEqual(attribute(Path('/mingw64/include/stdio.h'), 'mingw', [Path('/mingw64/include')]), 'mingw-w64')
        self.assertEqual(attribute(Path('/SDK/MacOSX.sdk/usr/include/stdio.h'), 'macos', []), 'macos-libc')
        with tempfile.TemporaryDirectory(dir='.') as temp:
            p = Path(temp).resolve()
            self.assertEqual(search_paths(f'#include <...> search starts here:\n {p}\nEnd of search list.'), [p])

    def test_generation_sorting_filtering_and_invalid_list(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            root = Path(temp); file = root / 'header.h'; out = root / 'list.txt'
            file.write_text('#define Z 1\n#undef A\n#define __INTERNAL 1\n#define assert(x) x\n#define PRIu24 "u"\n')
            self.assertEqual(generate('glibc', {file}, out, ['fixture']), 2)
            self.assertEqual(read_list(out)[0], {'A', 'Z'})
            with self.assertRaises(ValueError):
                generate('glibc', {root / 'missing'}, out, ['missing'])
            out.write_text('NOT A NAME\n')
            with self.assertRaises(ValueError):
                read_list(out)

    def test_merge_rejects_old_broad_inventories(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            root = Path(temp); (root / 'data/extensions').mkdir(parents=True)
            (root / 'data/names').mkdir(); (root / 'data/names/standard-macros.txt').write_text('assert\n')
            for library in ['glibc', 'libstdc++', 'mingw-w64', 'macos-libc']:
                (root / f'data/extensions/{library}.txt').write_text('# Not generated\n')
            artifacts = root / 'artifacts'; artifacts.mkdir()
            (artifacts / 'glibc.txt').write_text('# generator ci/extensions/generate.py v1;\nBROAD\n')
            with patch('merge.ROOT', root), self.assertRaisesRegex(ValueError, 'v2'):
                merge(artifacts, root / 'out')
            (artifacts / 'glibc.txt').write_text('# generator ci/extensions/generate.py v2;\nREACHED\n')
            with patch('merge.ROOT', root):
                merge(artifacts, root / 'out')
            self.assertEqual(read_list(root / 'out/glibc.txt')[0], {'REACHED'})


if __name__ == '__main__':
    unittest.main()
