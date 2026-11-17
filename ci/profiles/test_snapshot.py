# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path, PureWindowsPath
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from snapshot import (capture, inventory, startup_origins, join_native, native_runs, cpu_values,
                      compatible_triple, data, run, ROOT)
from flags import operations
from matrix import matrix, job_matrices, UBUNTU_26_04


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

    def test_snapshot_compiler_without_build_records_approximate_provenance(self):
        with tempfile.TemporaryDirectory(dir='.') as temp, patch('snapshot.shutil.which', return_value='/compiler/g++'), patch('snapshot.run', side_effect=self.run_compiler):
            path = self.profile(temp)
            path.write_text(path.read_text().replace('build =', 'snapshot_compiler ='))
            output = capture(path, 'g++').read_text()
            self.assertIn('# snapshot-compiler: package=fixture=15.2.0', output)
            self.assertIn('# build: none', output)
            self.assertIn('# reproduced: no', output)
            with self.assertRaisesRegex(ValueError, 'provenance'):
                capture(path, 'g++', 'package=fixture=15.2.0')
            path.write_text(path.read_text().replace('{ package = "fixture=15.2.0" }', '{}'))
            with self.assertRaisesRegex(ValueError, 'snapshot_compiler'):
                capture(path, 'g++')

    def test_cses_exact_ubuntu_snapshot_metadata_without_reproduction(self):
        original = ROOT / 'data/judges/cses-gcc13-cpp23.toml'
        calls = []

        def compiler_run(compiler, *args):
            calls.append(args)
            if args[0] == '-dumpfullversion':
                return '13.3.0'
            if args[0] == '-dumpmachine':
                return 'x86_64-linux-gnu'
            if args[0] == '--version':
                return 'g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0'
            self.assertEqual(args[:5], ('-x', 'c++', '-std=c++23', '-O2', '-Wall'))
            return '#define __GNUC__ 13\n#define __GNUC_MINOR__ 3\n#define __GNUC_PATCHLEVEL__ 0\n#define __cplusplus 202100L\n'

        with tempfile.TemporaryDirectory(dir='.') as temp, patch('snapshot.shutil.which', return_value='/usr/bin/g++-13'), \
                patch('snapshot.run', side_effect=compiler_run):
            profile = Path(temp) / original.name
            profile.write_bytes(original.read_bytes())
            output = capture(profile, 'g++-13').read_text()
            self.assertIn('# snapshot-compiler: package=ubuntu-24.04:g++-13=13.3.0-6ubuntu2~24.04.1', output)
            self.assertIn('# build: none\n# reproduced: no', output)
            self.assertIn('# builtin-registry: none', output)
            self.assertIn('# dumpmachine: x86_64-linux-gnu', output)
            self.assertNotIn('ONLINE_JUDGE', output)
            self.assertFalse(any(args[-1].endswith('probe.cpp') for args in calls))
            with self.assertRaisesRegex(ValueError, 'provenance'):
                capture(profile, 'g++-13', 'package=ubuntu-24.04:g++-13=13.3.0-6ubuntu2~24.04.1')

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
        dumps = [{'A': 'a', 'B': 'b', 'C': 'c'}, {'A': 'a', 'B': 'other'}]
        # Native options take CPU bits from CPUID, so the named-CPU sweep never
        # establishes a state: constant and absent model names are unknown too.
        self.assertEqual(join_native(dumps, model), {n: ('unknown', None) for n in model})
        # Only command-line macro operations fix a model name on every CPU.
        results = join_native(dumps, model, fixed=frozenset(model))
        self.assertEqual(results, {'A': ('known', 'a'), 'B': ('defined-unknown', None), 'C': ('unknown', None), 'D': ('undefined', None), 'T': ('undefined', None)})
        with self.assertRaisesRegex(ValueError, 'OUTSIDE'):
            join_native([{}, {'OUTSIDE': '1'}], model)
        with self.assertRaisesRegex(ValueError, 'A'):
            join_native([{}, {'A': '1'}], model, tune_only=True)
        self.assertEqual(join_native([{'A': 'a'}, {'A': 'a'}], model, True), {'T': ('unknown', None)})
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
        self.assertEqual(joined['CPU'], ('known', '#define CPU 7'))  # fixed by -DCPU=7
        self.assertEqual(joined['A'], ('unknown', None))  # CPU-derived: never taken from the sweep
        self.assertEqual(native_runs('unused', ['-O2'], None, {}), ({}, []))

    def test_cpu_values_uses_compilers_complete_list(self):
        diagnostic = "cc1plus: note: valid arguments to ‘-march=’ switch are: a b native c; did you mean a?\n"
        with patch('snapshot.subprocess.run', return_value=subprocess.CompletedProcess([], 1, '', diagnostic)):
            self.assertEqual(cpu_values('g++', '-march'), ['a', 'b', 'c'])

    def test_windows_compiler_commands_preserve_native_paths(self):
        compiler = r'C:\MSYS2 tools\mingw64\bin\g++.exe'
        source = r'D:\work tree\.distill-snapshot-test\empty.cpp'
        with patch('snapshot.subprocess.run', return_value=subprocess.CompletedProcess([], 0, 'macros', '')) as execute:
            self.assertEqual(run(compiler, '-x', 'c++', '-dM', '-E', source), 'macros')
            execute.assert_called_once_with([compiler, '-x', 'c++', '-dM', '-E', source], check=True,
                                            text=True, encoding='utf-8', errors='replace',
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        diagnostic = 'valid arguments to -march= switch are: a b native;\n'
        with patch('snapshot.subprocess.run', return_value=subprocess.CompletedProcess([], 1, '', diagnostic)) as execute:
            self.assertEqual(cpu_values(compiler, '-march'), ['a', 'b'])
            execute.assert_called_once_with([compiler, '-march=distill-invalid-cpu', '-x', 'c++', '-E', '-'],
                                            input='', text=True, capture_output=True, timeout=30, env=None)
        trace = '# 1 "D:\\\\work tree\\\\.distill-snapshot-test\\\\empty.cpp"\n#define X 1\n'
        self.assertEqual(startup_origins(trace, PureWindowsPath(source)), {'X': 'compiler-builtin'})

    def test_compiler_reads_closed_absolute_temp_files_with_spaces(self):
        compiler = str(Path('compiler with spaces/g++.exe').resolve())

        def execute(command, **kwargs):
            self.assertEqual(command[0], compiler)
            if command[-1].endswith('.cpp'):
                self.assertTrue(Path(command[-1]).is_absolute())
                self.assertIn('snapshot space ', command[-1])
                # The fixture reads both source files during the subprocess
                # call, after the writer has closed them (required on Windows).
            return subprocess.CompletedProcess(command, 0, self.run_compiler(*command), '')

        with tempfile.TemporaryDirectory(prefix='snapshot space ', dir='.') as temp, \
                patch('snapshot.shutil.which', return_value=compiler), \
                patch('snapshot.subprocess.run', side_effect=execute):
            capture(self.profile(temp), compiler)
            self.assertEqual(list(Path(temp).glob('.distill-snapshot-*')), [])

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
        self.assertEqual({r['profile'] for r in rows}, {'atcoder-gcc15-gnupp23', 'codeforces-gcc14-cpp23', 'cses-gcc13-cpp23', 'qoj-gcc15-cpp23'})
        self.assertEqual(matrix('atcoder-gcc15-gnupp23')['include'][0]['version'], '15.2.0')
        self.assertEqual(matrix('codeforces-gcc14-cpp23')['include'][0]['runner'], 'windows-2025')
        self.assertEqual(matrix('codeforces-gcc14-cpp23')['include'][0]['build'], '')
        self.assertEqual(matrix('codeforces-gcc14-cpp23')['include'][0]['compiler'], 'package=msys2:mingw-w64-x86_64-gcc=14.2.0-3')
        cses = matrix('cses-gcc13-cpp23')['include'][0]
        self.assertEqual(cses, dict(profile='cses-gcc13-cpp23', version='13.3.0', runner='ubuntu-24.04',
                                   build='', compiler='package=ubuntu-24.04:g++-13=13.3.0-6ubuntu2~24.04.1',
                                   acquisition='ubuntu'))
        self.assertEqual(matrix('qoj-gcc15-cpp23')['include'][0],
                         dict(profile='qoj-gcc15-cpp23', version='15.2.0', runner='ubuntu-24.04',
                              build='', compiler='package=ubuntu-26.04:g++-15=15.2.0-16ubuntu1',
                              acquisition='ubuntu', container=UBUNTU_26_04))
        self.assertEqual(matrix('atcoder-gcc15-gnupp23')['include'][0]['acquisition'], 'atcoder')
        self.assertEqual(matrix('codeforces-gcc14-cpp23')['include'][0]['acquisition'], 'msys2')
        with patch('matrix.tomllib.loads', return_value={'compiler': {'version': '14.2.0'}, 'triple': 'x86_64-w64-mingw32',
                   'build': {'package': 'msys2:mingw-w64-x86_64-gcc=14.2.0-1'},
                   'snapshot_compiler': {'package': 'msys2:mingw-w64-x86_64-gcc=14.2.0-3'}}):
            self.assertEqual(matrix('codeforces-gcc14-cpp23')['include'][0]['compiler'], 'package=msys2:mingw-w64-x86_64-gcc=14.2.0-1')
        with patch('matrix.tomllib.loads', return_value={'compiler': {'version': '13.3.0'},
                   'triple': 'x86_64-linux-gnu', 'snapshot_compiler': {'package': 'other:gcc=13.3.0-1'}}):
            with self.assertRaisesRegex(ValueError, 'unsupported compiler acquisition'):
                matrix('cses-gcc13-cpp23')
        for selection in ['', 'typo', 'atcoder-gcc15-gnupp23,', 'all,atcoder-gcc15-gnupp23']:
            with self.assertRaises(ValueError):
                matrix(selection)

    def test_job_partitions_skip_empty_matrices_and_keep_runner_rows(self):
        all_jobs = job_matrices()
        self.assertTrue(all_jobs['has_runner'])
        self.assertTrue(all_jobs['has_container'])
        self.assertEqual(all_jobs['runner_matrix']['include'],
                         matrix('atcoder-gcc15-gnupp23,codeforces-gcc14-cpp23,cses-gcc13-cpp23')['include'])
        self.assertTrue(all('container' not in row for row in all_jobs['runner_matrix']['include']))
        self.assertEqual(all_jobs['container_matrix'], matrix('qoj-gcc15-cpp23'))
        container_only = job_matrices('qoj-gcc15-cpp23,qoj-gcc15-cpp23')
        self.assertFalse(container_only['has_runner'])
        self.assertEqual(container_only['runner_matrix'], {'include': []})
        self.assertEqual(container_only['container_matrix'], matrix('qoj-gcc15-cpp23'))
        runner_only = job_matrices('cses-gcc13-cpp23')
        self.assertFalse(runner_only['has_container'])
        self.assertEqual(runner_only['container_matrix'], {'include': []})
        self.assertEqual(runner_only['runner_matrix'], matrix('cses-gcc13-cpp23'))


if __name__ == '__main__':
    unittest.main()
