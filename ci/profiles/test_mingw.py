# SPDX-License-Identifier: GPL-3.0-or-later
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from mingw import (REPOSITORY, Listing, closure, metadata, package_filename, prepare, satisfies)
from provenance import identity

GCC = 'mingw-w64-x86_64-gcc-14.2.0-3-any.pkg.tar.zst'
LIBS = 'mingw-w64-x86_64-gcc-libs-14.2.0-3-any.pkg.tar.zst'
PROVENANCE = 'package=msys2:mingw-w64-x86_64-gcc=14.2.0-3'


def info(name, version='14.2.0-3', **fields):
    return {'pkgname': ['mingw-w64-x86_64-' + name], 'pkgver': [version], **fields}


class MingwTests(unittest.TestCase):
    def test_exact_compiler_identity_and_no_version_fallback(self):
        self.assertEqual(package_filename(PROVENANCE), GCC)
        for value in ['14.2.0', 'package=msys2:gcc=14.2.0-3', PROVENANCE + '/bad', 'recipe=https://example.org']:
            with self.subTest(value=value), self.assertRaises(ValueError):
                package_filename(value)

    def test_provenance_schema(self):
        self.assertEqual(identity({}, 'snapshot_compiler'), 'none')
        self.assertEqual(identity({'snapshot_compiler': {'package': PROVENANCE.removeprefix('package=')}}, 'snapshot_compiler'), PROVENANCE)
        for value in [{}, [], 'x', {'package': 'gcc'}, {'package': ''}, {'recipe': 'file:///x'},
                      {'package': 'gcc=1\n2'}, {'package': 12}, {'typo': 'x'}, {'package': 'gcc=1', 'recipe': 'https://x'}]:
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'snapshot_compiler'):
                identity({'snapshot_compiler': value}, 'snapshot_compiler')

    def test_listing_uses_full_href_and_ignores_signatures(self):
        self.assertEqual(Listing(f'<a href="{GCC}">truncated..&gt;</a><a href="{GCC}.sig">sig</a>').files, {GCC})

    def test_transitive_dependencies_provides_cycles_and_pkgrel(self):
        packages = {
            GCC: info('gcc', depend=['mingw-w64-x86_64-gcc-libs=14.2.0-3', 'mingw-w64-x86_64-crt-git']),
            LIBS: info('gcc-libs', depend=['mingw-w64-x86_64-crt=13.0.0']),
            'crt': info('crt', '13.0.0-1', provides=['mingw-w64-x86_64-crt-git=13.0.0'], depend=['mingw-w64-x86_64-gcc-libs']),
        }
        self.assertEqual(closure(GCC, packages), sorted(packages))
        packages['crt']['pkgver'] = ['13.0.1-1']
        with self.assertRaisesRegex(ValueError, 'dependency mingw-w64-x86_64-crt=13.0.0'):
            closure(GCC, packages)

    def test_missing_dependency_revision_or_provider_fails(self):
        for packages in [{GCC: info('gcc', depend=['mingw-w64-x86_64-gcc-libs=14.2.0-3'])},
                         {GCC: info('gcc', depend=['mingw-w64-x86_64-gcc-libs=14.2.0-3']), LIBS: info('gcc-libs', '14.2.0-1')}]:
            with self.subTest(packages=packages), self.assertRaisesRegex(ValueError, 'dependency .*gcc-libs=14.2.0-3'):
                closure(GCC, packages)
        with self.assertRaisesRegex(ValueError, 'outside .*closure'):
            closure(GCC, {GCC: info('gcc'), LIBS: info('gcc-libs')})

    def test_version_bounds_use_pacman_vercmp(self):
        with patch('mingw.subprocess.check_output', return_value='-1\n') as compare:
            self.assertFalse(satisfies('2.9-1', '>=', '2.10-1'))
            self.assertTrue(satisfies('2.9-1', '<', '2.10-1'))
            compare.assert_called_with(['vercmp', '2.9-1', '2.10-1'], text=True)
        self.assertTrue(satisfies('14.2.0-3', '=', '14.2.0'))
        self.assertFalse(satisfies('14.2.0-3', '=', '14.2.0-1'))

    def test_metadata_identity_must_match_filename(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            path = Path(temp) / GCC
            for name in ['gcc', 'gcc-libs']:
                with tarfile.open(path, 'w') as archive:
                    data = f'pkgname = mingw-w64-x86_64-{name}\npkgver = 14.2.0-3\n'.encode()
                    entry = tarfile.TarInfo('.PKGINFO')
                    entry.size = len(data)
                    archive.addfile(entry, io.BytesIO(data))
                if name == 'gcc':
                    self.assertEqual(metadata(path), info('gcc'))
                else:
                    with self.assertRaisesRegex(ValueError, 'identity disagrees'):
                        metadata(path)

    def test_missing_repository_file_stops_before_package_downloads(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            directory = Path(temp)
            lock = directory / 'lock.txt'
            lock.write_text(GCC + '\n' + LIBS + '\n')
            def download(url, path):
                self.assertEqual(url, REPOSITORY)
                path.write_text(f'<a href="{GCC}">gcc</a>')
            with patch('mingw.download', side_effect=download), self.assertRaisesRegex(ValueError, 'Exact package unavailable: .*gcc-libs-14.2.0-3'):
                prepare(PROVENANCE, lock, directory)

    def test_download_failure_is_not_replaced(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            directory = Path(temp)
            lock = directory / 'lock.txt'
            lock.write_text(GCC + '\n')
            calls = []
            def download(url, path):
                calls.append(url)
                if url == REPOSITORY:
                    path.write_text(f'<a href="{GCC}">gcc</a>')
                else:
                    raise subprocess.CalledProcessError(22, ['curl', url])
            with patch('mingw.download', side_effect=download), self.assertRaises(subprocess.CalledProcessError):
                prepare(PROVENANCE, lock, directory)
            self.assertEqual(calls, [REPOSITORY, REPOSITORY + GCC])


if __name__ == '__main__':
    unittest.main()
