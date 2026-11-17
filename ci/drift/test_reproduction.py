# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import tempfile
import unittest

from common import ROOT, profiles
from reproduction import compare, SUFFIXES


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
