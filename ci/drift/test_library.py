# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from common import ROOT, profiles, validate
from library import compare, expected
from report import prepare


class LibraryDriftTests(unittest.TestCase):
    def test_missing_changed_and_failed_pipeline_are_never_passes(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = Path(directory)
            shipped = root / 'shipped.json'
            shipped.write_bytes(b'old\n')
            selected = profiles()
            def run(build='success'):
                return compare(selected, root, build, shipped)
            self.assertTrue(all(r['status'] == 'missing artifact' for r in run()['results']))
            artifact = root / 'library-generated/library.json'
            artifact.parent.mkdir()
            artifact.write_bytes(b'old\n')
            batch = run()
            self.assertTrue(all(r['status'] == 'identical' for r in batch['results']))
            validate(batch, 'library', selected, expected(selected), batch['run_id'])
            self.assertTrue(all(r['status'] == 'build failure' for r in run('failure')['results']))
            artifact.write_bytes(b'new dependency _GLIBCXX_NEW\n')
            changed = run()
            self.assertTrue(all(r['status'] == 'different' for r in changed['results']))
            self.assertIn('_GLIBCXX_NEW', changed['results'][0]['detail'])
            changed['run_id'] = '42'
            operations = prepare([changed], [])
            self.assertEqual(len(operations), len(selected))
            self.assertTrue(all(title.endswith(': library') for _, title, _, _ in operations))
            shipped.unlink()
            self.assertTrue(all(r['status'] == 'different' for r in run()['results']))

    def test_cli_produces_complete_failure_report(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = Path(directory)
            output = root / 'result.json'
            process = subprocess.run([sys.executable, str(ROOT / 'ci/drift/library.py'),
                '--profiles', 'timus-gcc13-cpp23', '--artifacts', str(root),
                '--build-result', 'failure', '--output', str(output)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 1)
            batch = json.loads(output.read_text())
            self.assertTrue(batch['complete'])
            self.assertEqual(batch['results'][0]['status'], 'missing artifact')
