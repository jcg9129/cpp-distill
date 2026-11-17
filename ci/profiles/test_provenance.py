# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact distro package identities; no compiler or package manager is executed."""
import unittest
from provenance import identity


class UbuntuProvenance(unittest.TestCase):
    def test_exact_release_package_revision_and_optional_epoch(self):
        for key in ('build', 'snapshot_compiler'):
            for package in ('ubuntu-24.04:g++-13=13.3.0-6ubuntu2~24.04.1',
                            'ubuntu-24.04:g++-13=1:13.3.0-6ubuntu2~24.04.1'):
                with self.subTest(key=key, package=package):
                    self.assertEqual(identity({key: {'package': package}}, key), 'package=' + package)

    def test_inexact_or_malformed_ubuntu_identity_rejected(self):
        for key in ('build', 'snapshot_compiler'):
            for package in ('ubuntu:g++-13=13.3.0-1', 'ubuntu-24:g++-13=13.3.0-1',
                            'ubuntu-24.04:=13.3.0-1', 'ubuntu-24.04:g++-13=13.3.0',
                            'ubuntu-24.04:g++-13=13.3.0-', 'ubuntu-24.04:g++-13=13.3.0-*',
                            'ubuntu-24.04:g++-13=13.3.0-1 extra', 'ubuntu-24.04:g++-13=13.3.0-1\n'):
                with self.subTest(key=key, package=package), self.assertRaisesRegex(ValueError, key):
                    identity({key: {'package': package}}, key)


if __name__ == '__main__':
    unittest.main()
