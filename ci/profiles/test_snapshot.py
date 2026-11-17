# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path, PureWindowsPath
import subprocess
import tempfile
import tomllib
import unittest
from unittest.mock import patch
from snapshot import (capture, inventory, startup_origins, join_native, native_runs, cpu_values,
                      compatible_triple, data, run, ROOT)
from flags import operations
from matrix import matrix, job_matrices, UBUNTU_26_04
from homebrew import PROVENANCE as HOMEBREW_PROVENANCE
from winlibs import PROVENANCE as WINLIBS_PROVENANCE

KATTIS_IMAGE = 'docker.io/kattis/judge-cpp@sha256:453998e7fefcf47fb86c094be4718f2986c0af3e7a079c7bd5696a60929d514d'
LIBRARY_CHECKER_IMAGE = 'docker.io/library/gcc@sha256:3ae15afe768b06d0c0fe088d822ba5f8045c26630bdacc8d8e7713cf5d8e7289'


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

    def test_kattis_image_snapshot_flags_and_approximate_metadata(self):
        original = ROOT / 'data/judges/kattis-gcc15-gnupp23.toml'
        calls = []

        def compiler_run(compiler, *args):
            calls.append(args)
            if args[0] == '-dumpfullversion':
                return '15.2.0'
            if args[0] == '-dumpmachine':
                return 'x86_64-linux-gnu'
            if args[0] == '--version':
                return 'g++ (synthetic image fixture) 15.2.0'
            self.assertEqual(args[:5], ('-x', 'c++', '-std=gnu++23', '-g', '-O2'))
            self.assertFalse(any(a.startswith(('-Wl,', '-l')) or a == '-static' for a in args))
            if args[-1].endswith('probe.cpp'):
                return ''.join(f'DISTILL_PROBE_{i} 1\n' for i in range(len(data('builtins', 'gcc-15.2'))))
            return '#define __GNUC__ 15\n#define __GNUC_MINOR__ 2\n#define __GNUC_PATCHLEVEL__ 0\n#define __cplusplus 202302L\n'

        with tempfile.TemporaryDirectory(dir='.') as temp, patch('snapshot.shutil.which', return_value='/usr/bin/g++-15'), \
                patch('snapshot.run', side_effect=compiler_run):
            profile = Path(temp) / original.name
            profile.write_bytes(original.read_bytes())
            output = capture(profile, 'g++-15').read_text()
            self.assertIn('# snapshot-compiler: image=' + KATTIS_IMAGE, output)
            self.assertIn('# build: none\n# reproduced: no', output)
            self.assertIn('# flag: -g\n# flag: -O2\n', output)
            self.assertNotIn('ONLINE_JUDGE', output)
            self.assertTrue(any(args[-1].endswith('probe.cpp') for args in calls))
            with self.assertRaisesRegex(ValueError, 'provenance'):
                capture(profile, 'g++-15', 'image=' + KATTIS_IMAGE)

    def test_library_checker_image_native_capture_and_host_regression(self):
        original = ROOT / 'data/judges/library-checker-gcc15-cpp23.toml'
        profile = tomllib.loads(original.read_text())
        self.assertEqual(profile['alias'], 'library-checker')
        self.assertEqual(profile['predefined'], original.stem + '-predefined.txt')
        self.assertEqual(profile['probe_source_bytes'], 1048576)
        self.assertNotIn('probe_output_bytes', profile)
        self.assertNotIn('build', profile)
        self.assertEqual(profile['link'], [])
        calls = []
        outside_model = False

        def compiler_run(compiler, *args):
            calls.append(args)
            self.assertEqual(compiler, '/synthetic/g++')
            if args[0] == '-dumpfullversion':
                return '15.2.0'
            if args[0] == '-dumpmachine':
                return 'x86_64-linux-gnu'
            if args[0] == '--version':
                return 'g++ (synthetic docker-library fixture) 15.2.0'
            self.assertEqual(args[:6], ('-x', 'c++', '-std=c++23', '-O2', '-DEVAL', '-DONLINE_JUDGE'))
            self.assertFalse(any(a.startswith('-I') or a == '/opt/ac-library' for a in args))
            if args[-1].endswith('probe.cpp'):
                return ''.join(f'DISTILL_PROBE_{i} 1\n' for i in range(len(data('builtins', 'gcc-15.2'))))
            definitions = ('#define __GNUC__ 15\n#define __GNUC_MINOR__ 2\n#define __GNUC_PATCHLEVEL__ 0\n'
                           '#define __cplusplus 202302L\n#define __STRICT_ANSI__ 1\n#define __SSE2__ 1\n')
            if '-march=haswell' in args:
                definitions += '#define __AVX2__ 1\n'
            if '-march=native' in args:
                # A host-only ISA bit must remain unknown, never undefined
                # just because all named presets lack it (native fix #33/#39).
                definitions += '#define __SHSTK__ 1\n'
                if outside_model:
                    definitions += '#define __UNMODELLED_HOST_FEATURE__ 1\n'
            command = '#define EVAL 1\n#define ONLINE_JUDGE 1\n'
            if '-dD' in args:
                return '# 0 "<built-in>"\n' + definitions + '# 0 "<command-line>"\n' + command
            return definitions + command

        with tempfile.TemporaryDirectory(dir='.') as temp, \
                patch('snapshot.shutil.which', return_value='/synthetic/g++'), \
                patch('snapshot.cpu_values', return_value=['x86-64', 'haswell']) as cpus, \
                patch('snapshot.run', side_effect=compiler_run):
            path = Path(temp) / original.name
            # Synthetic protocol output only; never create shipped snapshots.
            path.write_text(original.read_text().replace(profile['predefined'], 'capture.txt'))
            output = capture(path, 'g++').read_text()
            self.assertIn('# snapshot-compiler: image=' + LIBRARY_CHECKER_IMAGE, output)
            self.assertIn('# build: none\n# reproduced: no', output)
            self.assertIn('# std: c++23\n# builtin-registry: gcc-15.2\n# native-model: gcc-15.2-x86', output)
            self.assertIn('# flag: -O2\n# flag: -DEVAL\n# flag: -DONLINE_JUDGE\n# flag: -march=native', output)
            for name in ('EVAL', 'ONLINE_JUDGE'):
                self.assertIn('# command-operation #define ' + name + ' 1', output)
            for line in data('native', 'gcc-15.2-x86'):
                self.assertIn('# state ' + line.split()[0] + ' unknown native-model\n', output)
            cpus.assert_called_once_with('/synthetic/g++', '-march')
            self.assertEqual([a[6] for a in calls if '-dM' in a],
                             ['-march=native', '-march=x86-64', '-march=haswell'])
            outside_model = True
            with self.assertRaisesRegex(ValueError, 'native dependency model error: __UNMODELLED_HOST_FEATURE__'):
                capture(path, 'g++')

    def test_timus_native_capture_never_trusts_host_presence_or_absence(self):
        original = ROOT / 'data/judges/timus-gcc13-cpp23.toml'
        profile = tomllib.loads(original.read_text())
        self.assertEqual(profile['alias'], 'timus')
        self.assertEqual(profile['libraries'], ['mingw-w64', 'libstdc++'])
        self.assertEqual(profile['link'], ['-static', '-Wl,-s,--stack=67108864'])
        self.assertEqual(profile['inert'], ['-fno-strict-aliasing'])
        self.assertEqual(operations(profile['flags'], profile['inert']), [('ONLINE_JUDGE', '#define ONLINE_JUDGE 1')])
        with self.assertRaisesRegex(ValueError, 'unmodelled'):
            operations(profile['flags'])
        self.assertNotIn('build', profile)
        self.assertNotIn('probe_source_bytes', profile)
        self.assertNotIn('probe_output_bytes', profile)
        outside_model = False
        calls = []

        def compiler_run(compiler, *args):
            calls.append(args)
            if args[0] == '-dumpfullversion':
                return '13.2.0'
            if args[0] == '-dumpmachine':
                return 'x86_64-w64-mingw32'
            if args[0] == '--version':
                return 'g++ (synthetic WinLibs fixture) 13.2.0'
            self.assertEqual(args[:5], ('-x', 'c++', '-std=c++23', '-O2', '-fno-strict-aliasing'))
            self.assertEqual(args[6], '-DONLINE_JUDGE')
            self.assertNotIn('-static', args)
            self.assertFalse(any(a.startswith('-Wl,') for a in args))
            if args[-1].endswith('probe.cpp'):
                source = Path(args[-1]).read_text()
                self.assertNotIn('__has_embed', source)
                return ''.join(f'DISTILL_PROBE_{i} 1\n' for i in range(len(data('builtins', 'gcc-13.2'))))
            definitions = ('#define __GNUC__ 13\n#define __GNUC_MINOR__ 2\n#define __GNUC_PATCHLEVEL__ 0\n'
                           '#define __cplusplus 202100L\n#define __STRICT_ANSI__ 1\n#define __SSE2__ 1\n')
            if '-march=haswell' in args:
                definitions += '#define __AVX2__ 1\n'
            if '-march=native' in args:
                definitions += '#define __SHSTK__ 1\n'
                if outside_model:
                    definitions += '#define __UNMODELLED_HOST_FEATURE__ 1\n'
            command = '#define ONLINE_JUDGE 1\n'
            if '-dD' in args:
                return '# 0 "<built-in>"\n' + definitions + '# 0 "<command-line>"\n' + command
            return definitions + command

        with tempfile.TemporaryDirectory(dir='.') as temp, \
                patch('snapshot.shutil.which', return_value='/synthetic/g++.exe'), \
                patch('snapshot.cpu_values', return_value=['x86-64', 'haswell']), \
                patch('snapshot.run', side_effect=compiler_run):
            path = Path(temp) / original.name
            path.write_text(original.read_text().replace(profile['predefined'], 'capture.txt'))
            output = capture(path, '/synthetic/g++.exe').read_text()
            self.assertIn('# snapshot-compiler: ' + WINLIBS_PROVENANCE, output)
            self.assertIn('# build: none\n# reproduced: no', output)
            self.assertIn('# std: c++23\n# builtin-registry: gcc-13.2\n# native-model: gcc-13.2-x86', output)
            self.assertIn('#define __cplusplus 202100L', output)
            self.assertIn('#define __STRICT_ANSI__ 1', output)
            self.assertIn('# command-operation #define ONLINE_JUDGE 1', output)
            for line in data('native', 'gcc-13.2-x86'):
                self.assertIn('# state ' + line.split()[0] + ' unknown native-model\n', output)
            # Includes host-only SHSTK, always-present SSE2 and always-absent AVX512F.
            self.assertEqual([a[5] for a in calls if '-dM' in a],
                             ['-march=native', '-march=x86-64', '-march=haswell'])
            outside_model = True
            with self.assertRaisesRegex(ValueError, 'native dependency model error: __UNMODELLED_HOST_FEATURE__'):
                capture(path, '/synthetic/g++.exe')
            with self.assertRaisesRegex(ValueError, 'unsupported native model/ABI'), \
                    patch('snapshot.data', side_effect=lambda kind, key: []):
                capture(path, '/synthetic/g++.exe')

    def test_yukicoder_dialects_have_independent_capture_and_homebrew_metadata(self):
        for mode, std in [('cpp23', 'c++23'), ('gnupp23', 'gnu++23')]:
            original = ROOT / f'data/judges/yukicoder-gcc16-{mode}.toml'
            profile = tomllib.loads(original.read_text())
            self.assertEqual(profile.get('alias'), 'yukicoder' if mode == 'cpp23' else None)
            self.assertEqual(profile['predefined'], original.stem + '-predefined.txt')
            self.assertEqual(profile['probe_source_bytes'], 131072)
            self.assertNotIn('probe_output_bytes', profile)
            self.assertNotIn('build', profile)
            self.assertEqual(profile['link'], ['-lm'])
            calls = []

            def compiler_run(compiler, *args):
                calls.append(args)
                if args[0] == '-dumpfullversion':
                    return '16.2.0'
                if args[0] == '-dumpmachine':
                    return 'x86_64-pc-linux-gnu'
                if args[0] == '--version':
                    return 'g++-16 (Homebrew GCC 16.2.0) 16.2.0'
                self.assertEqual(args[:6], ('-x', 'c++', '-std=' + std, '-O2', '-Wuninitialized', '-DONLINE_JUDGE'))
                self.assertFalse(any(a.startswith(('-fplugin', '-lm', '-march')) for a in args))
                if args[-1].endswith('probe.cpp'):
                    self.assertIn('#ifdef __has_embed', Path(args[-1]).read_text())
                    return ''.join(f'DISTILL_PROBE_{i} 1\n' for i in range(len(data('builtins', 'gcc-16.2'))))
                return ('#define __GNUC__ 16\n#define __GNUC_MINOR__ 2\n#define __GNUC_PATCHLEVEL__ 0\n'
                        '#define __cplusplus 202302L\n#define ONLINE_JUDGE 1\n'
                        + ('#define __STRICT_ANSI__ 1\n' if mode == 'cpp23' else ''))

            with self.subTest(mode=mode), tempfile.TemporaryDirectory(dir='.') as temp, \
                    patch('snapshot.shutil.which', return_value='/synthetic/g++-16'), \
                    patch('snapshot.run', side_effect=compiler_run):
                # Synthetic protocol output only, never a new shipped snapshot.
                path = Path(temp) / original.name
                path.write_text(original.read_text().replace(profile['predefined'], 'capture.txt'))
                output = capture(path, '/synthetic/g++-16').read_text()
                self.assertIn('# snapshot-compiler: ' + HOMEBREW_PROVENANCE, output)
                self.assertIn('# build: none\n# reproduced: no', output)
                self.assertIn('# std: ' + std, output)
                self.assertIn('# builtin-registry: gcc-16.2\n# native-model: none', output)
                self.assertIn('# flag: -O2\n# flag: -Wuninitialized\n# flag: -DONLINE_JUDGE', output)
                self.assertIn('# command-operation #define ONLINE_JUDGE 1', output)
                self.assertIn('# state __has_embed defined-unknown special-builtin', output)
                self.assertEqual('#define __STRICT_ANSI__ 1' in output, mode == 'cpp23')
                self.assertEqual(sum(a[-1].endswith('probe.cpp') for a in calls), 1)
                with self.assertRaisesRegex(ValueError, 'provenance'):
                    capture(path, '/synthetic/g++-16', HOMEBREW_PROVENANCE)

    def test_yukicoder_matrix_rows_use_ubuntu_runner_and_separate_artifacts(self):
        names = ['yukicoder-gcc16-cpp23', 'yukicoder-gcc16-gnupp23']
        self.assertEqual(matrix(','.join(names))['include'], [
            dict(profile=name, version='16.2.0', runner='ubuntu-24.04', build='',
                 compiler=HOMEBREW_PROVENANCE, acquisition='homebrew') for name in names])
        jobs = job_matrices(','.join(names))
        self.assertTrue(jobs['has_runner'])
        self.assertFalse(jobs['has_container'])

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
        for key in ['gcc-13.2', 'gcc-14.2', 'gcc-15.2', 'gcc-16.2']:
            builtins = data('builtins', key)
            self.assertEqual('__has_extension' in builtins, key != 'gcc-13.2')
            self.assertEqual('__has_feature' in builtins, key != 'gcc-13.2')
            self.assertIn('__has_c_attribute', builtins)
            self.assertIn('__FILE_NAME__', builtins)
            self.assertEqual('__has_embed' in builtins, key not in ('gcc-13.2', 'gcc-14.2'))
            if key == 'gcc-16.2':
                self.assertEqual(builtins, data('builtins', 'gcc-15.2'))
                self.assertEqual(data('native', key + '-x86'), [])
                continue
            model = dict(x.split() for x in data('native', key + '-x86'))
            for n in ['__AVX2__', '__znver4', '__znver4__', '__k8', '__FP_FAST_FMA', '__FP_FAST_FMAF32x']:
                self.assertEqual(model[n], 'isa')
            for n in ['__tune_znver4__', '__GCC_DESTRUCTIVE_SIZE', '__GCC_CONSTRUCTIVE_SIZE']:
                self.assertEqual(model[n], 'tune')
            if key in ('gcc-13.2', 'gcc-15.2'):
                for n in ['__amd64', '__amd64__', '__x86_64', '__x86_64__', '_ILP32', '__ILP32__',
                          '__code_model_small__', '__code_model_medium__', '__code_model_large__',
                          '__code_model_32__', '__code_model_kernel__', '__SEG_FS', '__SEG_GS', '__SIZEOF_FLOAT128__']:
                    self.assertNotIn(n, model)
        self.assertEqual(len(data('builtins', 'gcc-13.2')), 17)
        model13 = dict(x.split() for x in data('native', 'gcc-13.2-x86'))
        for n in ['__SHSTK__', '__AVX512F__', '__AVX512ER__', '__AVX512PF__', '__knl__', '__knm__']:
            self.assertEqual(model13[n], 'isa')
        # GCC 13.3 uses a distinct key; CSES remains unsupported/approximate.
        self.assertEqual(data('builtins', 'gcc-13.3'), [])
        self.assertEqual(data('native', 'gcc-13.3-x86'), [])
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
        # -g adds GCC's working-directory marker; only the actual cwd is accepted.
        cwd = str(Path.cwd()).replace('\\', '/')
        traced = startup_origins(f'# 0 "/empty.cpp"\n# 0 "{cwd}//"\n# 0 "<built-in>"\n#define X 1\n', Path('/empty.cpp'))
        self.assertEqual(traced, {'X': 'compiler-builtin'})
        with self.assertRaisesRegex(ValueError, 'unexpected startup header'):
            startup_origins('# 0 "/somewhere/else//"\n#define X 1\n', Path('/empty.cpp'))

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

    def test_native_host_run_joins_regression_check(self):
        dump = lambda compiler, *args: '#define CPU 7\n#define ABI 1\n'
        with patch('snapshot.cpu_values', return_value=['cpu1']), patch('snapshot.run', side_effect=dump):
            joined, _ = native_runs('g++', ['-march=native'], Path('/empty.cpp'), {'CPU': 'isa'},
                                    {'CPU': '#define CPU 8', 'ABI': '#define ABI 1'})
            self.assertEqual(joined['CPU'], ('unknown', None))
            with self.assertRaisesRegex(ValueError, 'native dependency model error: ABI'):
                native_runs('g++', ['-march=native'], Path('/empty.cpp'), {'CPU': 'isa'},
                            {'CPU': '#define CPU 7', 'ABI': '#define ABI 2'})

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
        self.assertEqual({r['profile'] for r in rows}, {'atcoder-gcc15-gnupp23', 'codeforces-gcc14-cpp23', 'cses-gcc13-cpp23', 'qoj-gcc15-cpp23', 'kattis-gcc15-gnupp23', 'library-checker-gcc15-cpp23', 'timus-gcc13-cpp23', 'yukicoder-gcc16-cpp23', 'yukicoder-gcc16-gnupp23'})
        self.assertEqual(matrix('timus-gcc13-cpp23')['include'][0],
                         dict(profile='timus-gcc13-cpp23', version='13.2.0', runner='windows-2025',
                              build='', compiler=WINLIBS_PROVENANCE, acquisition='winlibs'))
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
        self.assertEqual(matrix('kattis-gcc15-gnupp23')['include'][0],
                         dict(profile='kattis-gcc15-gnupp23', version='15.2.0', runner='ubuntu-24.04',
                              build='', compiler='image=' + KATTIS_IMAGE,
                              acquisition='image', container=KATTIS_IMAGE, cxx='g++-15', native=False))
        self.assertEqual(matrix('library-checker-gcc15-cpp23')['include'][0],
                         dict(profile='library-checker-gcc15-cpp23', version='15.2.0', runner='ubuntu-24.04',
                              build='', compiler='image=' + LIBRARY_CHECKER_IMAGE,
                              acquisition='image', container=LIBRARY_CHECKER_IMAGE, cxx='g++', native=True))
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
        with patch('matrix.tomllib.loads', return_value={'compiler': {'version': '15.2.0'},
                   'triple': 'x86_64-linux-gnu', 'snapshot_compiler': {'image': 'docker.io/kattis/judge-cpp:latest'}}):
            with self.assertRaisesRegex(ValueError, 'snapshot_compiler'):
                matrix('kattis-gcc15-gnupp23')
        for selection in ['', 'typo', 'atcoder-gcc15-gnupp23,', 'all,atcoder-gcc15-gnupp23']:
            with self.assertRaises(ValueError):
                matrix(selection)

    def test_job_partitions_skip_empty_matrices_and_keep_runner_rows(self):
        all_jobs = job_matrices()
        self.assertTrue(all_jobs['has_runner'])
        self.assertTrue(all_jobs['has_container'])
        self.assertEqual(all_jobs['runner_matrix']['include'],
                         matrix('atcoder-gcc15-gnupp23,codeforces-gcc14-cpp23,cses-gcc13-cpp23,timus-gcc13-cpp23,yukicoder-gcc16-cpp23,yukicoder-gcc16-gnupp23')['include'])
        self.assertTrue(all('container' not in row for row in all_jobs['runner_matrix']['include']))
        self.assertEqual(all_jobs['container_matrix'], matrix('kattis-gcc15-gnupp23,library-checker-gcc15-cpp23,qoj-gcc15-cpp23'))
        container_only = job_matrices('qoj-gcc15-cpp23,qoj-gcc15-cpp23')
        self.assertFalse(container_only['has_runner'])
        self.assertEqual(container_only['runner_matrix'], {'include': []})
        self.assertEqual(container_only['container_matrix'], matrix('qoj-gcc15-cpp23'))
        runner_only = job_matrices('cses-gcc13-cpp23')
        self.assertFalse(runner_only['has_container'])
        self.assertEqual(runner_only['container_matrix'], {'include': []})
        self.assertEqual(runner_only['runner_matrix'], matrix('cses-gcc13-cpp23'))
        image_only = job_matrices('kattis-gcc15-gnupp23,kattis-gcc15-gnupp23')
        self.assertFalse(image_only['has_runner'])
        self.assertTrue(image_only['has_container'])
        self.assertEqual(image_only['runner_matrix'], {'include': []})
        self.assertEqual(image_only['container_matrix'], matrix('kattis-gcc15-gnupp23'))
        library_checker_only = job_matrices('library-checker-gcc15-cpp23')
        self.assertFalse(library_checker_only['has_runner'])
        self.assertTrue(library_checker_only['has_container'])
        self.assertEqual(library_checker_only['runner_matrix'], {'include': []})
        self.assertEqual(library_checker_only['container_matrix'], matrix('library-checker-gcc15-cpp23'))


if __name__ == '__main__':
    unittest.main()
