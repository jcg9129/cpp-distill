# SPDX-License-Identifier: GPL-3.0-or-later
"""LLVM-free guards against false passes in compatibility evidence."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch

from smoke import Smoke


class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.smoke = Smoke.__new__(Smoke)
        self.analysis = ('pruning analysis (does not certify an artifact)\n'
                         'parse configuration: "-std=gnu++23" "-target" "x86_64-linux-gnu"\n')
        self.skip = "distill: wrote flat (local), runtime skipped: can't run target binaries here\n"

    def load_profile_fixture(self, options):
        path = Path(options[options.index('--judge') + 1])
        with path.open('rb') as stream:
            profile = tomllib.load(stream)
        # Mirror src/core/profile/profile.cpp's name and relative snapshot rules.
        self.assertRegex(profile['name'], r'\A[a-z0-9]+(?:-[a-z0-9]+)*-gcc[1-9][0-9]*-(?:cpp|gnupp)[0-9]{2}(?:-[a-z0-9]+)*\Z')
        self.assertEqual(path.name, profile['name'] + '.toml')
        if 'predefined' in profile:
            snapshot = profile['predefined']
            self.assertNotIn(snapshot, ('.', '..'))
            self.assertFalse(any(char in snapshot for char in '/\\:'))
            self.assertTrue((path.parent / snapshot).is_file())
        return profile

    def test_generated_command_fact_profile_matches_loader_naming(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd(), prefix='.matrix-test-') as temp:
            self.smoke.work = Path(temp)
            self.smoke.triple = 'x86_64-linux-gnu'
            self.smoke.version = '14.2.0'
            output = Path(temp) / 'output.cpp'
            output.write_text('int main() { return 0; }\n', encoding='utf-8')
            for no_prune in (True, False):
                artifact = 'flat' if no_prune else 'pruned'

                def invoke(name, fixture, dialect, options, **kwargs):
                    profile = self.load_profile_fixture(options)
                    self.assertNotIn('predefined', profile)
                    report = (self.analysis + 'pruned: passed\nflat: passed\n#if: resolved true\n'
                              'option change: "-DDISTILL_FIXTURE_JUDGE=0"\n'
                              f"profile: {profile['name']}\n")
                    return subprocess.CompletedProcess([], 0, '', ''), report, output, artifact

                with self.subTest(no_prune=no_prune), patch.object(self.smoke, 'invoke', side_effect=invoke) as call:
                    self.smoke.profile('gnu++20', no_prune)
                    call.assert_called_once()

    def test_cross_adapter_preserves_profile_and_snapshot_names(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd(), prefix='.matrix-test-') as temp:
            self.smoke.work = Path(temp)
            self.smoke.triple = 'x86_64-w64-mingw32'
            output = Path(temp) / 'output.cpp'
            output.write_text('int main() {}\n', encoding='utf-8')

            def invoke(name, fixture, dialect, options):
                if options[1] == 'codeforces':
                    report = (self.skip + 'cannot find -lstdc++exp\nflat rejected:\n'
                              'configuration check: triple=x86_64-w64-mingw32\n')
                    return subprocess.CompletedProcess([], 0, '', ''), report, output, 'raw'
                profile = self.load_profile_fixture(options)
                self.assertEqual(profile['predefined'], 'codeforces-gcc14-cpp23-predefined.txt')
                self.assertEqual((self.smoke.work / profile['predefined']).read_bytes(),
                                 Path('data/judges/codeforces-gcc14-cpp23-predefined.txt').read_bytes())
                report = (self.skip + 'flat: passed\n'
                          '  configuration check: x86_64-w64-mingw32-g++ triple=x86_64-w64-mingw32\n')
                return subprocess.CompletedProcess([], 0, '', ''), report, output, 'flat'

            with patch.object(self.smoke, 'invoke', side_effect=invoke) as call:
                self.smoke.cross('cross-flat', 'no_profile', True)
                self.assertEqual(call.call_count, 2)

    def test_pruned_requires_checked_candidate(self):
        self.smoke.analysis(self.analysis + 'pruned: passed\n', 'pruned')
        with self.assertRaises(AssertionError):
            self.smoke.analysis(self.analysis, 'pruned')

    def test_flat_requires_reported_analysis_refusal(self):
        reason = 'predefined macro differs or unknown: __GNUC__'
        self.assertEqual(self.smoke.analysis(self.analysis + 'pruning disabled: ' + reason + '\n', 'flat'), reason)
        with self.assertRaises(AssertionError):
            self.smoke.analysis(self.analysis + 'flat: passed\n', 'flat')

    def test_buildable_flat_cannot_hide_parse_or_worker_failure(self):
        for reason in ('Clang parse error', 'invalid Clang parse configuration',
                       'unsupported Clang target', 'error-recovery AST: broken',
                       'pruning worker failed', 'pruning worker executable unavailable'):
            with self.subTest(reason=reason), self.assertRaises(AssertionError):
                self.smoke.analysis(self.analysis + 'pruning disabled: ' + reason + '\nflat: passed\n', 'flat')

    def test_missing_analysis_or_parse_argv_is_not_compatibility(self):
        for text in ('flat: passed\n', 'pruning analysis (does not certify an artifact)\n'):
            with self.subTest(text=text), self.assertRaises(AssertionError):
                self.smoke.analysis(text + 'pruning disabled: unknown conditional in user code\n', 'flat')

    def test_raw_is_never_a_passing_parse_row(self):
        with self.assertRaises(AssertionError):
            self.smoke.analysis(self.analysis + 'pruned: passed\n', 'raw')

    def test_cross_skip_without_target_invocation(self):
        self.smoke.cross_runtime(self.skip + 'process flat checks cwd="work" argv: "/usr/bin/x86_64-w64-mingw32-g++" "flat.cpp" "-o" "flat.exe"\n')

    def test_cross_skip_status_cannot_hide_attempted_execution(self):
        with self.assertRaises(AssertionError):
            self.smoke.cross_runtime(self.skip + 'process flat checks cwd="work" argv: "/work/flat.exe"\n')
        with self.assertRaises(AssertionError):
            self.smoke.cross_runtime('runtime compared 1 samples\n')

    def test_timeout_retains_command_and_bounded_diagnostics(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd(), prefix='.matrix-test-') as temp:
            root = Path(temp)
            fixture = root / 'fixtures/simple'
            fixture.mkdir(parents=True)
            (fixture / 'main.cpp').write_text('int main(){}\n', encoding='utf-8')
            self.smoke.work = root / 'work'
            self.smoke.work.mkdir()
            self.smoke.fixtures = root / 'fixtures'
            self.smoke.output = root / 'logs'
            self.smoke.output.mkdir()
            self.smoke.binary = root / 'distill'
            self.smoke.gcc = 'g++'
            with patch('smoke.run', side_effect=subprocess.TimeoutExpired('distill', 120, b'partial stdout', b'partial stderr')):
                with self.assertRaises(subprocess.TimeoutExpired):
                    self.smoke.invoke('deadline', 'simple', 'c++17')
            log = (self.smoke.output / 'deadline.log').read_text(encoding='utf-8')
            self.assertIn('TIMEOUT after 120 seconds', log)
            self.assertIn('partial stderr', log)
            self.assertIn('source space café', log)

    def test_summary_preserves_failed_rows_and_escapes_table_cells(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd(), prefix='.matrix-test-') as temp:
            root = Path(temp)
            (root / 'compiler').mkdir()
            row = dict(platform='linux-x86_64', gcc='g++ 14.2.0', dialect='c++23',
                       check='c++23', result='error', reason='parse | error\nnext line', passed=False)
            (root / 'compiler/rows.json').write_text(json.dumps([row]), encoding='utf-8')
            summary = root / 'job-summary.md'
            with patch.dict('os.environ', {'GITHUB_STEP_SUMMARY': str(summary)}):
                subprocess.run([sys.executable, str(Path(__file__).with_name('summary.py')), root],
                               check=True, capture_output=True, timeout=120)
            markdown = summary.read_text(encoding='utf-8')
            self.assertIn('parse \\| error<br>next line', markdown)
            self.assertIn('False', markdown)
            with (root / 'compatibility.tsv').open(encoding='utf-8', newline='') as stream:
                rows = list(csv.DictReader(stream, delimiter='\t'))
            self.assertEqual(rows[0]['reason'], row['reason'])
            self.assertEqual(rows[0]['passed'], 'False')


if __name__ == '__main__':
    unittest.main()
