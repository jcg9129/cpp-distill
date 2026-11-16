# SPDX-License-Identifier: GPL-3.0-or-later
"""LLVM-free guards against false passes in compatibility evidence."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from smoke import Smoke


class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.smoke = Smoke.__new__(Smoke)
        self.analysis = ('pruning analysis (does not certify an artifact)\n'
                         'parse configuration: "-std=gnu++23" "-target" "x86_64-linux-gnu"\n')
        self.skip = "distill: wrote flat (local), runtime skipped: can't run target binaries here\n"

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
