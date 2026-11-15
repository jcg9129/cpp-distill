# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from snapshot import capture
from matrix import matrix


class SnapshotTests(unittest.TestCase):
    def profile(self, directory):
        path = Path(directory) / 'fixture.toml'
        path.write_text('''name = "fixture"
compiler = { family = "gcc", version = "15.2.0" }
triple = "aarch64-linux-gnu"
std = "gnu++23"
defines = ["ONLINE_JUDGE"]
predefined = "fixture-predefined.txt"
''')
        return path

    def run_compiler(self, compiler, *args):
        if args[0] == '-dumpfullversion':
            return '15.2.0'
        if args[0] == '-dumpmachine':
            return 'aarch64-linux-gnu'
        if args[0] == '--version':
            return 'g++ fixture version\nCopyright'
        self.assertEqual(args[:6], ('-x', 'c++', '-std=gnu++23', '-dM', '-E', args[5]))
        self.assertEqual(Path(args[-1]).read_bytes(), b'')
        self.assertFalse(any(arg.startswith('-D') for arg in args))
        return '#define __GNUC__ 15\n#define __GNUC_MINOR__ 2\n#define __GNUC_PATCHLEVEL__ 0\n#define __cplusplus 202302L\n'

    def test_sorted_inventory_header_and_no_judge_defines(self):
        with tempfile.TemporaryDirectory(dir='.') as temp, patch('snapshot.shutil.which', return_value='/compiler/g++'), patch('snapshot.run', side_effect=self.run_compiler):
            path = self.profile(temp)
            path.write_text("\ufeff" + path.read_text(), encoding="utf-8")
            output = capture(path, 'g++').read_text()
            self.assertIn('compiler: /compiler/g++', output)
            self.assertIn('--version: g++ fixture version; date:', output)
            lines = [line for line in output.splitlines() if line.startswith('#define')]
            self.assertEqual(lines, sorted(lines))
            self.assertNotIn('#define ONLINE_JUDGE', output)
            self.assertEqual(len(list(Path(temp).glob('.distill-snapshot-*'))), 0)

    def test_mismatches_and_clang_are_rejected_without_output(self):
        for option, value in [('-dumpfullversion', '15.3.0'), ('-dumpmachine', 'x86_64-linux-gnu'), ('-x', '#define __GNUC__ 15\n#define __GNUC_MINOR__ 2\n#define __GNUC_PATCHLEVEL__ 0\n#define __clang__ 1'), ('-x', '#define __GNUC__ 15\n#define __GNUC_MINOR__ 2\n#define __GNUC_PATCHLEVEL__ 1')]:
            def compiler_run(compiler, *args):
                return value if args[0] == option else self.run_compiler(compiler, *args)
            with self.subTest(option=option, value=value), tempfile.TemporaryDirectory(dir='.') as temp, patch('snapshot.shutil.which', return_value='/compiler/g++'), patch('snapshot.run', side_effect=compiler_run):
                with self.assertRaises(ValueError):
                    capture(self.profile(temp), 'g++')
                self.assertFalse((Path(temp) / 'fixture-predefined.txt').exists())

    def test_missing_compiler_and_invalid_filename(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            path = self.profile(temp)
            with patch('snapshot.shutil.which', return_value=None), self.assertRaisesRegex(ValueError, 'compiler not found'):
                capture(path, 'absent')
            path.write_text(path.read_text().replace('fixture-predefined.txt', '../escape.txt'))
            with self.assertRaisesRegex(ValueError, 'next to the profile'):
                capture(path, 'g++')

    def test_matrix_all_selection_pins_and_bad_input(self):
        rows = matrix()['include']
        self.assertEqual({r['profile'] for r in rows}, {'atcoder', 'codeforces'})
        self.assertEqual(matrix('atcoder')['include'][0]['version'], '15.2.0')
        self.assertIn('@sha256:', matrix('atcoder')['include'][0]['image'])
        self.assertEqual(matrix('codeforces')['include'][0]['runner'], 'windows-2025')
        for selection in ['', 'typo', 'atcoder,', 'all,atcoder']:
            with self.assertRaises(ValueError):
                matrix(selection)


if __name__ == '__main__':
    unittest.main()
