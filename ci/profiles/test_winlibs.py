# SPDX-License-Identifier: GPL-3.0-or-later
"""WinLibs acquisition checks; all downloads/compiler calls are synthetic."""
from contextlib import redirect_stdout
import hashlib
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import winlibs


class WinlibsTests(unittest.TestCase):
    def test_streamed_archive_digest_is_checked_before_extraction(self):
        payload = b'synthetic archive bytes'
        digest = hashlib.sha256(payload).hexdigest()
        with tempfile.TemporaryDirectory(dir='.') as temp:
            destination = Path(temp) / 'compiler.zip'
            for expected in (digest, '0' * 64):
                with patch('winlibs.urlopen', return_value=io.BytesIO(payload)) as fetch, \
                        patch('winlibs.DIGEST', expected):
                    if expected == digest:
                        winlibs.download(destination)
                        self.assertEqual(destination.read_bytes(), payload)
                    else:
                        with self.assertRaisesRegex(winlibs.DigestError, 'SHA-256 mismatch'):
                            winlibs.download(destination)
                        self.assertFalse(destination.exists())
                    fetch.assert_called_once_with(winlibs.URL, timeout=60)

    def test_zip_extraction_requires_the_mingw64_tree(self):
        for name in ('mingw64/bin/g++.exe', '../escape', '/absolute', 'mingw64/../escape',
                     'C:/escape', 'mingw64\\escape', 'other/bin/g++.exe'):
            with self.subTest(name=name), tempfile.TemporaryDirectory(dir='.') as temp:
                root = Path(temp)
                archive = root / 'compiler.zip'
                with zipfile.ZipFile(archive, 'w') as zipped:
                    zipped.writestr(name, b'synthetic compiler')
                if name == 'mingw64/bin/g++.exe':
                    winlibs.extract(archive, root)
                    self.assertEqual((root / name).read_bytes(), b'synthetic compiler')
                else:
                    with self.assertRaisesRegex(ValueError, 'archive path'):
                        winlibs.extract(archive, root)

    def test_absolute_compiler_and_isolated_search_environment(self):
        compiler = Path('out/timus-winlibs/mingw64/bin/g++.exe').resolve()
        contaminated = dict.fromkeys(winlibs.CLEAN_VARIABLES, '/unexpected')
        contaminated.update(PATH='/msys64/mingw64/bin', SYSTEMROOT='C:/Windows')
        with patch.dict(os.environ, contaminated), patch('winlibs.subprocess.run') as run:
            run.return_value.stdout = 'fixture\n'
            self.assertEqual(winlibs.run(compiler, '-v'), 'fixture')
            env = run.call_args.kwargs['env']
            self.assertTrue(all(key not in env for key in winlibs.CLEAN_VARIABLES))
            system = Path('C:/Windows')
            self.assertEqual(env['PATH'], ';'.join(map(str, (compiler.parent, system / 'System32', system))))
            self.assertEqual(run.call_args.args[0], [str(compiler), '-v'])
            self.assertTrue(run.call_args.kwargs['check'])

    def test_acquisition_fails_without_substitution(self):
        with tempfile.TemporaryDirectory(dir='.') as temp, \
                patch('winlibs.platform.system', return_value='Windows'), \
                patch('winlibs.platform.machine', return_value='AMD64'), \
                patch('winlibs.download') as download, patch('winlibs.extract') as extract, \
                patch('winlibs.verify') as verify:
            for bad in (winlibs.PROVENANCE[:-1] + 'a', winlibs.PROVENANCE.replace('ucrt-r4', 'ucrt-r5'),
                        winlibs.PROVENANCE.removeprefix('winlibs=')):
                with self.subTest(bad=bad), self.assertRaises(ValueError):
                    winlibs.install(bad, Path(temp) / 'unused')
            download.assert_not_called()
            destination = Path(temp).resolve() / 'compiler'
            self.assertEqual(winlibs.install(winlibs.PROVENANCE, destination), destination / 'mingw64/bin/g++.exe')
            download.assert_called_once()
            extract.assert_called_once_with(download.call_args.args[0], destination)
            verify.assert_called_once_with(destination / 'mingw64/bin/g++.exe')
            with self.assertRaises(FileExistsError):
                winlibs.install(winlibs.PROVENANCE, destination)
            with patch('winlibs.platform.system', return_value='Linux'), self.assertRaisesRegex(ValueError, 'Windows'):
                winlibs.install(winlibs.PROVENANCE, Path(temp) / 'wrong-runner')

    def test_hash_failure_never_extracts(self):
        with tempfile.TemporaryDirectory(dir='.') as temp, \
                patch('winlibs.platform.system', return_value='Windows'), \
                patch('winlibs.platform.machine', return_value='AMD64'), \
                patch('winlibs.download', side_effect=ValueError('SHA-256 mismatch')), \
                patch('winlibs.extract') as extract, patch('winlibs.verify') as verify:
            with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
                winlibs.install(winlibs.PROVENANCE, Path(temp) / 'compiler')
            extract.assert_not_called()
            verify.assert_not_called()

    def test_verification_checks_configuration_and_logs_search_paths(self):
        verbose = ("Configured with: --enable-threads=posix "
                   "--with-pkgversion='MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders' "
                   '--host=x86_64-w64-mingw32\nThread model: posix\ngcc version 13.2.0')
        for failure in (None, 'with-threads', 'version', 'threads', 'crt', 'seh', 'target', 'dumpmachine', 'headers'):
            calls = []
            with self.subTest(failure=failure), tempfile.TemporaryDirectory(dir='.') as temp:
                compiler = Path(temp).resolve() / 'mingw64/bin/g++.exe'
                compiler.parent.mkdir(parents=True)
                compiler.touch()

                def run(cxx, *args):
                    self.assertEqual(cxx, compiler)
                    calls.append(args)
                    if args == ('-dumpfullversion',):
                        return '13.2.1' if failure == 'version' else '13.2.0'
                    if args == ('-v',):
                        return (verbose.replace('--enable-threads=posix', '--enable-threads=mcf') if failure == 'threads'
                                else verbose.replace('--enable-threads', '--with-threads') if failure == 'with-threads'
                                else verbose.replace('x86_64-ucrt', 'x86_64-msvcrt') if failure == 'crt'
                                else verbose.replace('posix-seh', 'posix-sjlj') if failure == 'seh'
                                else verbose.replace('x86_64-w64-mingw32', 'i686-w64-mingw32') if failure == 'target'
                                else verbose)
                    if args == ('-dumpmachine',):
                        return 'i686-w64-mingw32' if failure == 'dumpmachine' else 'x86_64-w64-mingw32'
                    if args[-1].endswith('configuration.cpp'):
                        source = Path(args[-1]).read_text()
                        self.assertIn('#include <cstdio>', source)
                        self.assertIn('__MINGW64_VERSION_BUGFIX != 1', source)
                        if failure == 'headers':
                            raise subprocess.CalledProcessError(1, args)
                    return 'synthetic diagnostics'

                with patch('winlibs.run', side_effect=run), redirect_stdout(io.StringIO()):
                    if failure not in (None, 'with-threads'):
                        with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                            winlibs.verify(compiler)
                    else:
                        winlibs.verify(compiler)
                        for args in [('-v',), ('-dumpmachine',), ('-print-search-dirs',),
                                     ('-E', '-v', '-x', 'c++', 'NUL')]:
                            self.assertIn(args, calls)


if __name__ == '__main__':
    unittest.main()
