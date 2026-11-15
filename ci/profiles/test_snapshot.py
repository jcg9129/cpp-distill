# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from snapshot import (capture, inventory, startup_origins, join_native, native_runs, cpu_values,
                      compatible_triple, data, ROOT)
from flags import operations
from matrix import matrix


class SnapshotTests(unittest.TestCase):
    def profile(self, directory):
        path = Path(directory) / 'fixture.toml'
        path.write_text('''name = "fixture"
compiler = { family = "gcc", version = "15.2.0" }
triple = "x86_64-linux-gnu"
std = "gnu++23"
flags = ["-O2", "-DX=1", "-UX", "-DONLINE_JUDGE"]
build = { package = "fixture=15.2.0" }
predefined = "fixture-predefined.txt"
''')
        return path

    def run_compiler(self, compiler, *args):
        if args[0] == '-dumpfullversion':
            return '15.2.0'
        if args[0] == '-dumpmachine':
            return 'x86_64-pc-linux-gnu'
        if args[0] == '--version':
            return 'g++ fixture version\nCopyright'
        self.assertEqual(args[:7], ('-x', 'c++', '-std=gnu++23', '-O2', '-DX=1', '-UX', '-DONLINE_JUDGE'))
        if args[-1].endswith('probe.cpp'):
            source = Path(args[-1]).read_text()
            self.assertIn('#ifdef __has_embed', source)
            return ''.join(f'DISTILL_PROBE_{i} 1\n' for i in range(len(data('builtins', 'gcc-15.2'))))
        self.assertEqual(Path(args[-1]).read_bytes(), b'')
        output = '#define __GNUC__ 15\n#define __GNUC_MINOR__ 2\n#define __GNUC_PATCHLEVEL__ 0\n#define __cplusplus 202302L\n#define ONLINE_JUDGE 1\n#define __STDC_IEC_559__ 1\n'
        if '-dD' in args:
            return '# 0 "<built-in>"\n' + output.replace('#define __STDC_IEC_559__ 1\n', '') + '# 1 "/usr/include/stdc-predef.h" 1 3 4\n#define __STDC_IEC_559__ 1\n'
        return output

    def test_full_flags_provenance_and_build_metadata(self):
        with tempfile.TemporaryDirectory(dir='.') as temp, patch('snapshot.shutil.which', return_value='/compiler/g++'), patch('snapshot.run', side_effect=self.run_compiler):
            path = self.profile(temp)
            output = capture(path, 'g++', 'package=fixture=15.2.0').read_text()
            self.assertIn('# compiler-path: /compiler/g++', output)
            self.assertIn('# compiler-version: g++ fixture version\\nCopyright', output)
            self.assertIn('# dumpmachine: x86_64-pc-linux-gnu', output)
            self.assertIn('# reproduced: yes', output)
            self.assertIn('# state X undefined command-operation', output)
            self.assertIn('# command-operation #define ONLINE_JUDGE 1', output)
            self.assertIn('# startup-header stdc-predef.h #define __STDC_IEC_559__ 1', output)
            self.assertIn('# state __has_include defined-unknown special-builtin', output)
            self.assertEqual(len(list(Path(temp).glob('.distill-snapshot-*'))), 0)
            self.assertIn('# reproduced: no', capture(path, 'g++').read_text())
            with self.assertRaisesRegex(ValueError, 'provenance'):
                capture(path, 'g++', 'package=other')

    def test_mismatches_clang_and_other_startup_headers_rejected(self):
        for option, value in [('-dumpfullversion', '15.3.0'), ('-dumpmachine', 'aarch64-linux-gnu'),
                              ('-dM', '#define __clang__ 1\n'), ('-dD', '# 1 "/judge/settings.h" 1\n')]:
            def compiler_run(compiler, *args):
                return value if option in args else self.run_compiler(compiler, *args)
            with self.subTest(option=option), tempfile.TemporaryDirectory(dir='.') as temp, patch('snapshot.shutil.which', return_value='/compiler/g++'), patch('snapshot.run', side_effect=compiler_run):
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

    def test_registry_and_model_source_coverage(self):
        for key in ['gcc-14.2', 'gcc-15.2']:
            builtins = data('builtins', key)
            self.assertIn('__has_extension', builtins)
            self.assertIn('__has_c_attribute', builtins)
            self.assertIn('__FILE_NAME__', builtins)
            self.assertEqual('__has_embed' in builtins, key == 'gcc-15.2')
            model = dict(x.split() for x in data('native', key + '-x86'))
            for n in ['__AVX2__', '__znver4', '__znver4__', '__k8', '__FP_FAST_FMA', '__FP_FAST_FMAF32x']:
                self.assertEqual(model[n], 'isa')
            for n in ['__tune_znver4__', '__GCC_DESTRUCTIVE_SIZE', '__GCC_CONSTRUCTIVE_SIZE']:
                self.assertEqual(model[n], 'tune')
        self.assertEqual(data('builtins', 'gcc-99.0'), [])

    def test_builtin_undef_and_ordinary_override(self):
        for flag, definition, probe_bit in [('-U__has_include', None, '0'), ('-D__has_include(x)=0', '#define __has_include(x) 0', '1')]:
            def compiler_run(compiler, *args):
                # Remove the extra flag to reuse fixture assertion.
                base = tuple(a for a in args if a != flag)
                output = self.run_compiler(compiler, *base)
                if args[-1].endswith('probe.cpp'):
                    i = data('builtins', 'gcc-15.2').index('__has_include')
                    return output.replace(f'DISTILL_PROBE_{i} 1', f'DISTILL_PROBE_{i} {probe_bit}')
                return output + (('# 0 \"<command-line>\"\n' if '-dD' in args else '') + definition + '\n' if definition and ('-dM' in args or '-dD' in args) else '')
            with tempfile.TemporaryDirectory(dir='.') as temp, patch('snapshot.shutil.which', return_value='/compiler/g++'), patch('snapshot.run', side_effect=compiler_run):
                path = self.profile(temp)
                path.write_text(path.read_text().replace('"-DONLINE_JUDGE"]', '"-DONLINE_JUDGE", "' + flag + '"]'))
                text = capture(path, 'g++').read_text()
                self.assertIn('# state __has_include undefined command-operation' if definition is None else '# command-operation ' + definition, text)

    def test_command_replacement_text_retains_whitespace(self):
        flag = '-DSPACES=a  +  b'
        def compiler_run(compiler, *args):
            output = self.run_compiler(compiler, *(a for a in args if a != flag))
            if '-dM' in args or '-dD' in args:
                return output + ('# 0 "<command-line>"\n' if '-dD' in args else '') + '#define SPACES a + b\n'
            return output
        with tempfile.TemporaryDirectory(dir='.') as temp, patch('snapshot.shutil.which', return_value='/compiler/g++'), patch('snapshot.run', side_effect=compiler_run):
            path = self.profile(temp)
            path.write_text(path.read_text().replace('"-DONLINE_JUDGE"]', '"-DONLINE_JUDGE", "' + flag + '"]'))
            self.assertIn('# command-operation #define SPACES a  +  b', capture(path, 'g++').read_text())

    def test_inventory_strict_and_startup_trace(self):
        for text in ['', '#undef X', '#define X 1\n#define X 2', 'garbage']:
            with self.assertRaises(ValueError):
                inventory(text)
        self.assertEqual(inventory('#define F(x) x\n#define EMPTY\n')['EMPTY'], '#define EMPTY ')
        origins = startup_origins('# 0 "<built-in>"\n#define X 1\n# 0 "<command-line>"\n#define Y 2\n# 1 "/usr/include/stdc-predef.h" 1 3 4\n#define Z 3\n', Path('/empty.cpp'))
        self.assertEqual(origins, {'X': 'compiler-builtin', 'Y': 'command-operation', 'Z': 'startup-header'})

    def test_native_four_state_join_and_outside_model_failure(self):
        model = {'A': 'isa', 'B': 'isa', 'C': 'isa', 'D': 'isa', 'T': 'tune'}
        results = join_native([{'A': 'a', 'B': 'b', 'C': 'c'}, {'A': 'a', 'B': 'other'}], model)
        self.assertEqual(results, {'A': ('known', 'a'), 'B': ('defined-unknown', None), 'C': ('unknown', None), 'D': ('undefined', None), 'T': ('undefined', None)})
        with self.assertRaisesRegex(ValueError, 'OUTSIDE'):
            join_native([{}, {'OUTSIDE': '1'}], model)
        with self.assertRaisesRegex(ValueError, 'A'):
            join_native([{}, {'A': '1'}], model, tune_only=True)
        self.assertEqual(join_native([{'A': 'a'}, {'A': 'a', 'T': '1'}], model, True), {'T': ('unknown', None)})
        with self.assertRaises(ValueError):
            join_native([], model)

    def test_native_replaces_in_place_and_preserves_later_operations(self):
        calls = []
        def dump(compiler, *args):
            calls.append(args)
            self.assertEqual(args[-4:], ('-DCPU=7', '-dM', '-E', '/empty.cpp'))
            return '#define CPU 7\n#define A ' + ('1' if '-march=cpu1' in args else '2') + '\n'
        with patch('snapshot.cpu_values', return_value=['cpu1', 'cpu2']), patch('snapshot.run', side_effect=dump):
            joined, accepted = native_runs('g++', ['-x', 'c++', '-march=native', '-DCPU=7'], Path('/empty.cpp'), {'CPU': 'isa', 'A': 'isa'})
        self.assertEqual(len(calls), 2)
        self.assertEqual(joined['CPU'], ('known', '#define CPU 7'))
        self.assertEqual(joined['A'], ('defined-unknown', None))
        self.assertEqual(native_runs('unused', ['-O2'], None, {}), ({}, []))

    def test_cpu_values_uses_compilers_complete_list(self):
        diagnostic = "cc1plus: note: valid arguments to ‘-march=’ switch are: a b native c; did you mean a?\n"
        with patch('snapshot.subprocess.run', return_value=subprocess.CompletedProcess([], 1, '', diagnostic)):
            self.assertEqual(cpu_values('g++', '-march'), ['a', 'b', 'c'])

    def test_flags_order_errors_and_inert(self):
        self.assertEqual(operations(['-DX', '-UX', '-D', 'X=', '-DF(x)=0']), [('X', '#define X 1'), ('X', None), ('X', '#define X '), ('F', '#define F(x) 0')])
        for flag in ['-std=c++23', '-includefoo', '-Wp,-Dfoo', '-Wl,x', '-specs=x', '-Bfoo', '@x', '-D2BAD', '-UX(x)']:
            with self.subTest(flag=flag), self.assertRaises(ValueError):
                operations([flag], [flag])
        self.assertEqual(operations(['-O2', '-Wall', '-fmodules', '-fopenmp', '-pthread', '-march=native', '-fconstexpr-depth=1024'], ['-fconstexpr-depth=1024']), [])

    def test_triple_only_vendor_compatibility(self):
        self.assertTrue(compatible_triple('x86_64-linux-gnu', 'x86_64-pc-linux-gnu'))
        self.assertFalse(compatible_triple('x86_64-linux-gnu', 'aarch64-linux-gnu'))
        self.assertFalse(compatible_triple('x86_64-linux-gnu', 'x86_64-linux-musl'))

    def test_matrix_selection_builds_and_bad_input(self):
        rows = matrix()['include']
        self.assertEqual({r['profile'] for r in rows}, {'atcoder', 'codeforces'})
        self.assertEqual(matrix('atcoder')['include'][0]['version'], '15.2.0')
        self.assertEqual(matrix('codeforces')['include'][0]['runner'], 'windows-2025')
        for selection in ['', 'typo', 'atcoder,', 'all,atcoder']:
            with self.assertRaises(ValueError):
                matrix(selection)


if __name__ == '__main__':
    unittest.main()
