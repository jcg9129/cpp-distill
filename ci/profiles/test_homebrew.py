# SPDX-License-Identifier: GPL-3.0-or-later
"""Homebrew acquisition checks with synthetic bytes and mocked external tools."""
from contextlib import redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import homebrew


class HomebrewTests(unittest.TestCase):
    def test_download_checks_blob_bytes_and_uses_anonymous_registry_token(self):
        payload = b'synthetic bottle bytes'
        digest = hashlib.sha256(payload).hexdigest()
        with tempfile.TemporaryDirectory(dir='.') as temp:
            destination = Path(temp) / 'bottle.tar.gz'
            for expected in (digest, '0' * 64):
                with patch('homebrew.urlopen', side_effect=[io.BytesIO(b'{"token":"anonymous-fixture"}'),
                                                           io.BytesIO(payload)]) as fetch:
                    if expected == digest:
                        homebrew.download(destination, expected)
                        self.assertEqual(destination.read_bytes(), payload)
                    else:
                        with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
                            homebrew.download(destination, expected)
                        self.assertFalse(destination.exists())
                    self.assertEqual(fetch.call_args_list[0].args[0],
                                     'https://ghcr.io/token?scope=repository:homebrew/core/gcc:pull')
                    request = fetch.call_args_list[1].args[0]
                    self.assertEqual(request.full_url, 'https://ghcr.io/v2/homebrew/core/gcc/blobs/sha256:' + expected)
                    self.assertEqual(request.get_header('Authorization'), 'Bearer anonymous-fixture')

    def test_download_errors_do_not_trigger_fallback(self):
        with tempfile.TemporaryDirectory(dir='.') as temp, patch('homebrew.urlopen', side_effect=OSError('unavailable')):
            with self.assertRaisesRegex(OSError, 'unavailable'):
                homebrew.download(Path(temp) / 'bottle.tar.gz', homebrew.DIGEST)

    def test_every_command_has_clean_include_and_library_environment(self):
        contaminated = dict.fromkeys(homebrew.CLEAN_VARIABLES, '/unexpected')
        with patch.dict(os.environ, contaminated), patch('homebrew.subprocess.run') as run:
            run.return_value.stdout = 'fixture\n'
            self.assertEqual(homebrew.run('/absolute/g++-16', '-v'), 'fixture')
            env = run.call_args.kwargs['env']
            self.assertTrue(all(key not in env for key in homebrew.CLEAN_VARIABLES))
            self.assertEqual(env['HOMEBREW_NO_AUTO_UPDATE'], '1')
            self.assertEqual(env['HOMEBREW_NO_INSTALL_UPGRADE'], '1')
            self.assertEqual(run.call_args.args[0], ['/absolute/g++-16', '-v'])
            self.assertTrue(run.call_args.kwargs['check'])

    def test_only_audited_identity_is_acquired(self):
        with patch('homebrew.download') as download, patch('homebrew.run') as run:
            for value in (homebrew.PROVENANCE[:-1] + 'a', homebrew.PROVENANCE.replace('16.2.0', '16.2.1'),
                          homebrew.PROVENANCE.removeprefix('homebrew='), 'homebrew=homebrew:gcc=16.2.0'):
                with self.subTest(value=value), self.assertRaises(ValueError):
                    homebrew.install(value)
            download.assert_not_called()
            run.assert_not_called()

    def install_fixture(self, temp, failure=None):
        prefix = Path(temp).resolve() / 'linuxbrew'
        brew = prefix / 'bin/brew'
        brew.parent.mkdir(parents=True)
        if failure != 'missing-brew':
            brew.touch()
        if failure == 'existing-keg':
            (prefix / 'Cellar/gcc/other').mkdir(parents=True)
        calls = []
        read_text = Path.read_text

        def read(path, *args, **kwargs):
            if path == Path('/etc/os-release'):
                return 'ID=ubuntu\nVERSION_ID="' + ('25.04' if failure == 'runner' else '24.04') + '"\n'
            return read_text(path, *args, **kwargs)

        def run(*argv):
            calls.append(tuple(map(str, argv)))
            if argv[1:] == ('--prefix',):
                return str(prefix)
            if argv[1] == 'install':
                self.assertEqual(argv[2], '--force-bottle')
                self.assertEqual(Path(argv[3]).name, 'gcc--16.2.0.x86_64_linux.bottle.1.tar.gz')
                if failure == 'install':
                    raise subprocess.CalledProcessError(1, argv)
                keg = prefix / 'Cellar/gcc/16.2.0'
                (keg / 'bin').mkdir(parents=True)
                (keg / 'bin/g++-16').touch()
                (prefix / 'bin/g++-16').symlink_to(keg / 'bin/g++-16' if failure != 'path' else brew)
                (keg / 'INSTALL_RECEIPT.json').write_text(json.dumps({
                    'poured_from_bottle': failure != 'receipt',
                    'source': {'versions': {'stable': '16.2.0'}}}))
            if argv[1:3] == ('list', '--versions'):
                return 'gcc ' + ('16.2.1' if failure == 'version' else '16.2.0')
            if argv[1:] == ('--version',):
                return 'wrong banner' if failure == 'banner' else homebrew.BANNER + '\nCopyright'
            if argv[1:] == ('-dumpfullversion',):
                return '16.2.1' if failure == 'dumpversion' else '16.2.0'
            return 'synthetic diagnostics'

        with patch('homebrew.BREW', brew), patch.object(Path, 'read_text', read), \
                patch('homebrew.platform.system', return_value='Linux'), \
                patch('homebrew.platform.machine', return_value='x86_64'), \
                patch('homebrew.run', side_effect=run), patch('homebrew.download') as download, \
                redirect_stdout(io.StringIO()):
            result = homebrew.install(homebrew.PROVENANCE)
            self.assertEqual(result, prefix / 'bin/g++-16')
            self.assertEqual(download.call_args.args[1], homebrew.DIGEST)
        return calls

    def test_exact_local_bottle_is_poured_and_compiler_diagnostics_are_logged(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            calls = self.install_fixture(temp)
        compiler = calls[-1][0]
        for args in [('--version',), ('-dumpfullversion',), ('-v',), ('-dumpmachine',),
                     ('-print-search-dirs',), ('-E', '-v', '-x', 'c++', '/dev/null')]:
            self.assertIn((compiler, *args), calls)
        self.assertEqual(sum(c[1] == 'install' for c in calls), 1)

    def test_missing_runner_brew_or_substitution_fails_without_retry(self):
        for failure in ('runner', 'missing-brew', 'existing-keg', 'install', 'version',
                        'receipt', 'path', 'banner', 'dumpversion'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory(dir='.') as temp:
                with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                    self.install_fixture(temp, failure)


if __name__ == '__main__':
    unittest.main()
