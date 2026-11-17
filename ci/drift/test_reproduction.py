# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import tempfile
import unittest

from common import ROOT
from reproduction import compare, SUFFIXES


class ReproductionTests(unittest.TestCase):
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
