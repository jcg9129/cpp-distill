# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit checks for the CI expected-output driver; subprocesses are mocked."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import probe


class ProbeCapture(unittest.TestCase):
    def test_exact_snapshot_flags_parts_and_upload_file(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
            root = Path(temp)
            profile = root / 'test.toml'
            profile.write_text('name="fixture-gcc15-gnupp23"\nstd="gnu++23"\nflags=["-O2","-DVALUE=","-UVALUE","-march=native"]\nlink=["-lnot-used-for-snapshot"]\n', encoding='utf-8')
            calls = []

            def run(argv):
                argv = list(map(str, argv))
                calls.append(argv)
                if '--judge-probe' in argv:
                    part = argv[-1]
                    if part == '1/1':
                        raise subprocess.CalledProcessError(1, argv, stderr=b'probe exceeds source/output byte budget')
                    return b'// fixture source ' + part.encode()
                if argv[0] == 'snapshot-g++':
                    return b''
                if '--judge-check' in argv:
                    self.assertEqual(Path(argv[-1]).read_bytes(), b'fixture output\nfixture output\n')
                    return b'user-reported match\n'
                if '--distill-expected' in argv:
                    return b'canonical expected output\n'
                return b'fixture output\n'

            with patch.object(probe, 'run', side_effect=run):
                out = probe.capture(profile, 'snapshot-g++', root / 'distill')
            self.assertEqual(out.name, 'fixture-gcc15-gnupp23-probe-expected.txt')
            self.assertEqual(out.read_bytes(), b'canonical expected output\ncanonical expected output\n')
            compiles = [a for a in calls if a[0] == 'snapshot-g++']
            self.assertEqual(len(compiles), 2)
            for argv in compiles:
                self.assertEqual(argv[1:8], ['-x', 'c++', '-std=gnu++23', '-O2', '-DVALUE=', '-UVALUE', '-march=native'])
                self.assertNotIn('-lnot-used-for-snapshot', argv)
            self.assertEqual(calls[-1][1], '--judge-check')

    def test_unexpected_generation_failure_is_not_retried(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
            profile = Path(temp) / 'test.toml'
            profile.write_text('name="fixture"\nstd="c++23"\nflags=[]\n', encoding='utf-8')
            with patch.object(probe, 'run', side_effect=subprocess.CalledProcessError(1, 'distill', stderr=b'bad snapshot')) as call:
                with self.assertRaises(subprocess.CalledProcessError):
                    probe.capture(profile, 'g++', Path(temp) / 'distill')
                self.assertEqual(call.call_count, 1)


    def test_every_part_must_fit_before_compiling(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
            root = Path(temp)
            profile = root / 'test.toml'
            profile.write_text('name="fixture"\nstd="c++23"\nflags=[]\n', encoding='utf-8')
            sources = []
            compiles = []

            def run(argv):
                argv = list(map(str, argv))
                if '--judge-probe' in argv:
                    part = argv[-1]
                    sources.append(part)
                    if part in ('1/1', '2/2'):
                        raise subprocess.CalledProcessError(1, argv, stderr=b'probe exceeds source/output byte budget')
                    return part.encode()
                if argv[0] == 'g++':
                    compiles.append(Path(argv[-3]).read_bytes())
                return b'fixture output\n'

            with patch.object(probe, 'run', side_effect=run):
                probe.capture(profile, 'g++', root / 'distill')
            self.assertEqual(sources, ['1/1', '1/2', '2/2', '1/3', '2/3', '3/3'])
            self.assertEqual(compiles, [b'1/3', b'2/3', b'3/3'])

    def test_output_over_budget_or_probe_failure_rejected(self):
        for output in (b'X' * 1001, b'expansion|X|2170726f62652d6661696c7572653a\n'):
            with self.subTest(output=output[:60]), tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
                root = Path(temp)
                profile = root / 'test.toml'
                profile.write_text('name="fixture"\nstd="c++23"\nflags=[]\nprobe_output_bytes=1000\n', encoding='utf-8')
                with patch.object(probe, 'run', side_effect=[b'// fixture source', b'', output]):
                    with self.assertRaises(ValueError):
                        probe.capture(profile, 'g++', root / 'distill')
                self.assertFalse((root / 'fixture-probe-expected.txt').exists())

    def test_expected_mode_budget_and_failures_are_also_checked(self):
        for output in (b'X' * 1001, b'expansion|X|2170726f62652d6661696c7572653a\n'):
            with self.subTest(output=output[:60]), tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
                root = Path(temp)
                profile = root / 'test.toml'
                profile.write_text('name="fixture"\nstd="c++23"\nflags=[]\nprobe_output_bytes=1000\n')
                with patch.object(probe, 'run', side_effect=[b'// source', b'', b'actual\n', output]):
                    with self.assertRaises(ValueError):
                        probe.capture(profile, 'g++', root / 'distill')
                self.assertFalse((root / 'fixture-probe-expected.txt').exists())


if __name__ == '__main__':
    unittest.main()
