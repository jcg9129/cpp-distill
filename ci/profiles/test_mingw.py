# SPDX-License-Identifier: GPL-3.0-or-later
import io
from pathlib import Path, PureWindowsPath
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from mingw import (REPOSITORY, Listing, closure, download, main, metadata,
                   package_filename, prepare, satisfies, tool)
from provenance import identity

GCC = 'mingw-w64-x86_64-gcc-14.2.0-3-any.pkg.tar.zst'
LIBS = 'mingw-w64-x86_64-gcc-libs-14.2.0-3-any.pkg.tar.zst'
PROVENANCE = 'package=msys2:mingw-w64-x86_64-gcc=14.2.0-3'
WINDOWS_TOOLS = {f'MSYS2_{name.upper()}': rf'C:\MSYS2 tools\usr\bin\{name}.exe'
                 for name in ('tar', 'zstd', 'curl', 'vercmp')}


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
        with patch('mingw.sys.platform', 'linux'), patch('mingw.subprocess.check_output', return_value='-1\n') as compare:
            self.assertFalse(satisfies('2.9-1', '>=', '2.10-1'))
            self.assertTrue(satisfies('2.9-1', '<', '2.10-1'))
            compare.assert_called_with(['vercmp', '2.9-1', '2.10-1'], text=True)
        self.assertTrue(satisfies('14.2.0-3', '=', '14.2.0'))
        self.assertFalse(satisfies('14.2.0-3', '=', '14.2.0-1'))

    def test_windows_requires_explicit_absolute_tools(self):
        with patch('mingw.sys.platform', 'win32'), patch.dict('mingw.os.environ', WINDOWS_TOOLS, clear=True):
            for name in ('tar', 'zstd', 'curl', 'vercmp'):
                self.assertEqual(tool(name), WINDOWS_TOOLS['MSYS2_' + name.upper()])
                for invalid in ('', name, '/usr/bin/' + name, 'C:' + name):
                    with self.subTest(name=name, invalid=invalid), patch.dict('mingw.os.environ', {'MSYS2_' + name.upper(): invalid}):
                        with self.assertRaisesRegex(ValueError, 'absolute Windows path'):
                            tool(name)
        with patch('mingw.sys.platform', 'darwin'), patch.dict('mingw.os.environ', WINDOWS_TOOLS):
            self.assertEqual(tool('tar'), 'tar')

    def test_windows_missing_tools_fail_before_download(self):
        with patch('mingw.sys.platform', 'win32'), patch.dict('mingw.os.environ', {}, clear=True), \
                patch('mingw.sys.argv', ['mingw.py', PROVENANCE]), patch('mingw.prepare') as prepare_mock, \
                patch('mingw.sys.stderr', new_callable=io.StringIO) as stderr:
            with self.assertRaises(SystemExit) as error:
                main()
            self.assertEqual(error.exception.code, 1)
            self.assertIn('MSYS2_TAR must be an absolute Windows path', stderr.getvalue())
            prepare_mock.assert_not_called()

    def test_windows_metadata_uses_explicit_tools_and_handles(self):
        with tempfile.TemporaryDirectory(prefix='mingw space ', dir='.') as temp:
            path = Path(temp) / GCC
            path.write_bytes(b'compressed archive')

            def decompress(command, *, stdin, stdout, check):
                self.assertEqual(command, [WINDOWS_TOOLS['MSYS2_ZSTD'], '-dc'])
                self.assertTrue(check)
                self.assertEqual(stdin.read(), b'compressed archive')
                stdout.write(b'decompressed archive')

            def extract(command, *, stdin, text, encoding):
                self.assertEqual(command, [WINDOWS_TOOLS['MSYS2_TAR'], '-xOf', '-', '.PKGINFO'])
                self.assertTrue(text)
                self.assertEqual(encoding, 'utf-8')
                self.assertEqual(stdin.read(), b'decompressed archive')
                return 'pkgname = mingw-w64-x86_64-gcc\npkgver = 14.2.0-3\n'

            with patch('mingw.sys.platform', 'win32'), patch.dict('mingw.os.environ', WINDOWS_TOOLS), \
                    patch('mingw.tarfile.open', side_effect=tarfile.ReadError), \
                    patch('mingw.subprocess.run', side_effect=decompress), \
                    patch('mingw.subprocess.check_output', side_effect=extract):
                self.assertEqual(metadata(path), info('gcc'))
            self.assertEqual(list(Path(temp).iterdir()), [path])

    def test_windows_decompression_failure_stops_extraction(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            path = Path(temp) / GCC
            path.write_bytes(b'bad archive')
            with patch('mingw.sys.platform', 'win32'), patch.dict('mingw.os.environ', WINDOWS_TOOLS), \
                    patch('mingw.tarfile.open', side_effect=tarfile.ReadError), \
                    patch('mingw.subprocess.run', side_effect=subprocess.CalledProcessError(1, ['zstd'])), \
                    patch('mingw.subprocess.check_output') as extract:
                with self.assertRaises(subprocess.CalledProcessError):
                    metadata(path)
                extract.assert_not_called()
            self.assertEqual(list(Path(temp).iterdir()), [path])

    def test_windows_curl_and_vercmp_commands(self):
        with patch('mingw.sys.platform', 'win32'), patch.dict('mingw.os.environ', WINDOWS_TOOLS), \
                patch('mingw.subprocess.run') as run, \
                patch('mingw.subprocess.check_output', return_value='1\r\n') as compare:
            destination = PureWindowsPath(r'D:\work tree\out\package.pkg.tar.zst')
            download(REPOSITORY + GCC, destination)
            run.assert_called_once_with([WINDOWS_TOOLS['MSYS2_CURL'], '--fail', '--location', '--retry', '3',
                                         REPOSITORY + GCC, '-o', 'D:/work tree/out/package.pkg.tar.zst'], check=True)
            self.assertTrue(satisfies('2.10-1', '>=', '2.9-1'))
            compare.assert_called_once_with([WINDOWS_TOOLS['MSYS2_VERCMP'], '2.10-1', '2.9-1'], text=True)

    def test_posix_metadata_fallback_command_unchanged(self):
        with patch('mingw.sys.platform', 'linux'), patch('mingw.tarfile.open', side_effect=tarfile.ReadError), \
                patch('mingw.subprocess.check_output', return_value='pkgname = mingw-w64-x86_64-gcc\npkgver = 14.2.0-3\n') as extract:
            path = Path('out') / GCC
            self.assertEqual(metadata(path), info('gcc'))
            extract.assert_called_once_with(['tar', '--force-local', '-xOf', path.as_posix(), '.PKGINFO'],
                                            text=True, encoding='utf-8')

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
