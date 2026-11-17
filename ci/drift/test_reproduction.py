# SPDX-License-Identifier: GPL-3.0-or-later
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from common import ROOT, profiles
from reproduction import compare, difference, SUFFIXES
from common import envelope, result, summarize


class ReproductionTests(unittest.TestCase):
    def test_all_shipped_outputs_are_compared_including_timus(self):
        selected = profiles()
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = Path(directory)
            for name in selected:
                artifact = root / ('snapshot-' + name)
                artifact.mkdir()
                for suffix in SUFFIXES:
                    filename = name + suffix
                    (artifact / filename).write_bytes((ROOT / 'data/judges' / filename).read_bytes())
            rows = compare(selected, root)['results']
            self.assertEqual(len(rows), 2 * len(selected))
            self.assertTrue(all(row['status'] == 'identical' for row in rows))
            timus = 'timus-gcc13-cpp23'
            (root / ('snapshot-' + timus) / (timus + SUFFIXES[0])).write_bytes(b'changed\n')
            (root / ('snapshot-' + timus) / (timus + SUFFIXES[1])).unlink()
            rows = compare(selected, root)['results']
            self.assertEqual([row['status'] for row in rows if row['profile'] == timus],
                             ['different', 'missing artifact'])
            self.assertTrue(all(row['status'] == 'identical' for row in rows if row['profile'] != timus))

    def test_exact_bytes_missing_and_no_normalization(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = Path(directory)
            shipped = root / 'shipped'
            shipped.mkdir()
            artifact = root / 'snapshot-p'
            artifact.mkdir()
            for suffix in SUFFIXES:
                (shipped / ('p' + suffix)).write_bytes(b'hello\n')
                (artifact / ('p' + suffix)).write_bytes(b'hello\n')
            def statuses():
                return [r['status'] for r in compare(['p'], root, shipped)['results']]
            self.assertEqual(statuses(), ['identical', 'identical'])
            (artifact / ('p' + SUFFIXES[0])).write_bytes(b'hello\r\n')
            self.assertEqual(statuses(), ['different', 'identical'])
            (artifact / ('p' + SUFFIXES[1])).unlink()
            self.assertEqual(statuses(), ['different', 'missing artifact'])
            self.assertTrue(all(r['status'] == 'missing artifact' for r in compare(['missing-profile'], root, shipped)['results']))

    def test_difference_shows_line_numbers_exact_bytes_and_bounded_diff(self):
        detail = difference('p.txt', b'first\nhello\n', b'first\nhello\r\n')
        self.assertIn('first differing line: 2', detail)
        self.assertIn('--- shipped/p.txt', detail)
        self.assertIn('+++ artifact/p.txt', detail)
        self.assertIn(r"hello\r\n", detail)
        self.assertIn('first differing line: 2', difference('p', b'a\n', b'a\nb\n'))
        self.assertIn('first differing line: 1', difference('p', b'a\n', b'a'))
        detail = difference('p', b'a\n' * 200, b'b\n' * 200)
        self.assertIn('diff truncated at 40 lines', detail)
        self.assertLessEqual(len(detail.splitlines()), 42)

    def test_cli_prints_failure_and_preserves_json_including_build_failure(self):
        selected = 'timus-gcc13-cpp23'
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = Path(directory)
            output = root / 'result.json'
            command = [sys.executable, str(ROOT / 'ci/drift/reproduction.py'),
                       '--profiles', selected, '--artifacts', str(root), '--output', str(output)]
            run = subprocess.run(command + ['--build-result', 'success'], capture_output=True, text=True)
            self.assertEqual(run.returncode, 1)
            self.assertIn(selected + '-predefined.txt: missing artifact', run.stdout)
            self.assertIn(selected + '-probe-expected.txt: missing artifact', run.stdout)
            self.assertEqual(len(json.loads(output.read_text())['results']), 2)
            artifact = root / ('snapshot-' + selected)
            artifact.mkdir()
            for suffix in SUFFIXES:
                filename = selected + suffix
                (artifact / filename).write_bytes((ROOT / 'data/judges' / filename).read_bytes())
            run = subprocess.run(command + ['--build-result', 'failure'], capture_output=True, text=True)
            self.assertEqual(run.returncode, 1)
            self.assertIn('profile regeneration workflow failed', run.stdout)
            self.assertTrue(all(row['status'] == 'build failure' for row in json.loads(output.read_text())['results']))
            filename = selected + SUFFIXES[1]
            (artifact / filename).write_bytes(b'changed\n')
            run = subprocess.run(command + ['--build-result', 'success'], capture_output=True, text=True)
            self.assertEqual(run.returncode, 1)
            self.assertIn(filename + ': different', run.stdout)
            self.assertIn('first differing line: 1', run.stdout)
            self.assertIn('--- shipped/' + filename, run.stdout)
            self.assertEqual(json.loads(output.read_text())['results'][1]['status'], 'different')

    def test_other_claims_also_log_classified_failures(self):
        for claim, status in [('evidence', 'extractor failure'), ('availability', 'integrity alert')]:
            stream = io.StringIO()
            with redirect_stdout(stream):
                summarize(envelope(claim, ['p'], [result('p', 'source', 'host', status, 'failure reason')]))
            self.assertIn('p: source: ' + status, stream.getvalue())
            self.assertIn('failure reason', stream.getvalue())
