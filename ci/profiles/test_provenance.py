# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact compiler identities; no compiler or package manager is executed."""
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


class ImageProvenance(unittest.TestCase):
    def test_snapshot_only_digest_pinned_image(self):
        image = 'docker.io/kattis/judge-cpp@sha256:' + 'a' * 64
        self.assertEqual(identity({'snapshot_compiler': {'image': image}}, 'snapshot_compiler'), 'image=' + image)
        with self.assertRaisesRegex(ValueError, 'build'):
            identity({'build': {'image': image}}, 'build')

    def test_tags_inexact_digests_and_malformed_images_rejected(self):
        image = 'docker.io/kattis/judge-cpp@sha256:' + 'a' * 64
        for value in ('kattis/judge-cpp@sha256:' + 'a' * 64,
                      'docker.io/kattis/judge-cpp', 'docker.io/kattis/judge-cpp:latest',
                      image.replace('@', ':latest@'), image[:-1], image + 'a',
                      image.replace('sha256:', 'sha512:'), image.replace('a' * 64, 'A' * 64),
                      image.replace('/kattis/', '/Kattis/'), image.replace('/kattis/', '//'),
                      image.replace('/kattis/', '/../'), image + ' ', image + '\n', '', 12):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'snapshot_compiler'):
                identity({'snapshot_compiler': {'image': value}}, 'snapshot_compiler')
        with self.assertRaisesRegex(ValueError, 'snapshot_compiler'):
            identity({'snapshot_compiler': {'image': image, 'package': 'gcc=15.2.0'}}, 'snapshot_compiler')


class HomebrewProvenance(unittest.TestCase):
    def test_snapshot_only_exact_core_gcc_bottle(self):
        value = 'homebrew:gcc=16.2.0@sha256:' + 'a' * 64
        self.assertEqual(identity({'snapshot_compiler': {'homebrew': value}}, 'snapshot_compiler'), 'homebrew=' + value)
        with self.assertRaisesRegex(ValueError, 'build'):
            identity({'build': {'homebrew': value}}, 'build')

    def test_inexact_or_malformed_homebrew_identity_rejected(self):
        value = 'homebrew:gcc=16.2.0@sha256:' + 'a' * 64
        for bad in (value.removeprefix('homebrew:'), value.replace('gcc=', 'gcc@16='),
                    value.replace('gcc=', 'llvm='), value.replace('16.2.0', '16.2'),
                    value.replace('16.2.0', '0.2.0'), value.replace('16.2.0', '16.2.0_1'),
                    value.replace('16.2.0', '*'), value.split('@')[0],
                    value.replace('@', ':latest@'), value[:-1], value + 'a',
                    value.replace('sha256:', 'sha512:'), value.replace('a' * 64, 'A' * 64),
                    value + ' ', value + '\n', '', 12):
            with self.subTest(value=bad), self.assertRaisesRegex(ValueError, 'snapshot_compiler'):
                identity({'snapshot_compiler': {'homebrew': bad}}, 'snapshot_compiler')
        for other in ('image', 'package', 'recipe'):
            with self.subTest(other=other), self.assertRaisesRegex(ValueError, 'snapshot_compiler'):
                identity({'snapshot_compiler': {'homebrew': value, other: 'gcc=16.2.0'}}, 'snapshot_compiler')


if __name__ == '__main__':
    unittest.main()
