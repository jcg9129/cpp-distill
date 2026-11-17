# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import tempfile
import unittest

import ubuntu

ROOT = Path(__file__).resolve().parents[2]


class UbuntuTests(unittest.TestCase):
    def test_exact_release_revision_and_nine_packages(self):
        for release, major, revision in [('24.04', '13', '13.3.0-6ubuntu2~24.04.1'), ('26.04', '15', '15.2.0-16ubuntu1')]:
            self.assertEqual(ubuntu.parse(f'package=ubuntu-{release}:g++-{major}={revision}'), (release, major, revision))
            self.assertEqual(ubuntu.snapshot(release)[1], '20261117T000000Z')
            packages = ubuntu.packages(major)
            self.assertEqual(len(set(packages)), 9)
            self.assertIn(f'g++-{major}-x86-64-linux-gnu', packages)
        for bad in ('package=ubuntu:g++-13=13.3.0', 'package=ubuntu-24.04:g++-13=13.3.0', 'package=ubuntu-24.04:g++-13=*'):
            with self.assertRaises(ValueError):
                ubuntu.parse(bad)

    def test_duplicate_missing_or_bad_snapshot(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            lock = Path(directory) / 'lock'
            for text in ['24.04 noble bad', '24.04 noble 20261117T000000Z\n24.04 noble 20261117T000000Z', '26.04 resolute 20261117T000000Z']:
                lock.write_text(text)
                with self.assertRaises(ValueError):
                    ubuntu.snapshot('24.04', lock)
