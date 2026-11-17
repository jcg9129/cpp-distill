# SPDX-License-Identifier: GPL-3.0-or-later
import base64
from copy import deepcopy
import hashlib
import io
import json
import lzma
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

import availability
import winlibs
from common import IntegrityError, profiles, verify_stream
from pgp import packets, signed_digest

FIXTURES = Path(__file__).with_name('fixtures')


class Response(io.BytesIO):
    status = 200
    headers = {}
    def __init__(self, url, data):
        super().__init__(data)
        self.url = url


def sha(data):
    return 'sha256:' + hashlib.sha256(data).hexdigest()


class AvailabilityTests(unittest.TestCase):
    def router(self, mapping):
        seen = []
        def open_request(request, **kwargs):
            url = request.full_url
            seen.append(url)
            if url not in mapping:
                self.fail('unexpected HTTP request: ' + url)
            return Response(url, mapping[url])
        return seen, patch('common.urlopen', side_effect=open_request)

    def image_fixture(self):
        task = {'url': 'https://registry.test/v2/test/image', 'repository': 'test/image'}
        base = task['url']
        config, layer = b'{"architecture":"amd64"}', b'layer bytes\x00' * 300000
        blobs = [{'digest': sha(b), 'size': len(b)} for b in (config, layer)]
        child = json.dumps({'schemaVersion': 2, 'config': blobs[0], 'layers': [blobs[1]]}).encode()
        index = json.dumps({'schemaVersion': 2, 'manifests': [{'digest': sha(child), 'size': len(child), 'platform': {'os': 'linux', 'architecture': 'amd64'}}]}).encode()
        task['digest'] = sha(index)
        urls = {'https://auth.docker.io/token?service=registry.docker.io&scope=repository:test/image:pull': b'{"token":"anonymous-test-token"}',
                base + '/manifests/' + sha(index): index, base + '/manifests/' + sha(child): child,
                base + '/blobs/' + sha(config): config, base + '/blobs/' + sha(layer): layer}
        return task, urls

    def test_image_downloads_index_child_config_and_every_layer(self):
        task, mapping = self.image_fixture()
        seen, network = self.router(mapping)
        with network:
            availability.image(task)
        self.assertEqual(set(seen), mapping.keys())

    def test_corrupt_image_index_child_or_layer_never_passes(self):
        task, mapping = self.image_fixture()
        for url in mapping:
            if '/manifests/' not in url and '/blobs/' not in url:
                continue
            broken = dict(mapping)
            broken[url] += b'X'
            _, network = self.router(broken)
            with self.subTest(url=url), network, self.assertRaises(IntegrityError):
                availability.image(task)

    def test_ubuntu_verifies_indexes_and_exact_full_deb(self):
        base = 'https://snapshot.test/ubuntu/time/'
        data = b'synthetic deb\x00' * 1000
        record = ('Package: g++-13\nVersion: 13.3.0-1\nArchitecture: amd64\nFilename: pool/main/g/gcc-13/example.deb\nSHA256: ' + sha(data).split(':')[1] + '\nSize: ' + str(len(data)) + '\n').encode()
        compressed = lzma.compress(record)
        release = ('SHA256:\n ' + sha(compressed).split(':')[1] + ' ' + str(len(compressed)) + ' main/binary-amd64/Packages.xz\n').encode()
        mapping = {base + 'pool/main/g/gcc-13/example.deb': data}
        for suite in ('noble', 'noble-updates', 'noble-security'):
            mapping[base + 'dists/' + suite + '/Release'] = release
            mapping[base + 'dists/' + suite + '/main/binary-amd64/Packages.xz'] = compressed
        task = dict(url=base, codename='noble', package='g++-13', revision='13.3.0-1')
        seen, network = self.router(mapping)
        with network:
            availability.ubuntu_package(task, {})
        self.assertEqual(set(seen), mapping.keys())
        for url in [base + 'pool/main/g/gcc-13/example.deb', base + 'dists/noble/main/binary-amd64/Packages.xz']:
            broken = dict(mapping)
            broken[url] += b'x'
            _, network = self.router(broken)
            with network, self.assertRaises(IntegrityError):
                availability.ubuntu_package(task, {})
        _, network = self.router(mapping)
        with network, self.assertRaisesRegex(ValueError, 'absent'):
            availability.ubuntu_package(dict(task, revision='wrong'), {})

    def test_msys2_acquisition_streams_signed_package(self):
        fixture = json.loads((FIXTURES / 'signed-sha512.json').read_text())
        payload = base64.b64decode(fixture['payload'])
        signature = base64.b64decode(fixture['signature'])
        url = 'https://repo.test/exact.pkg.tar.zst'
        task = dict(url=url, method='msys2')
        with tempfile.TemporaryDirectory(dir=availability.ROOT) as directory:
            root = Path(directory)
            (root / 'ci/drift').mkdir(parents=True)
            (root / 'ci/drift/msys2-keys.json').write_text(json.dumps({fixture['fingerprint']: [fixture['n'], fixture['e']]}))
            for data, succeeds in [(payload, True), (payload + b'tampered', False)]:
                seen, network = self.router({url: data, url + '.sig': signature})
                with patch.object(availability, 'ROOT', root), network:
                    if succeeds:
                        availability.check(task, {})
                    else:
                        with self.assertRaises(IntegrityError):
                            availability.check(task, {})
                self.assertEqual(set(seen), {url, url + '.sig'})

    def test_bottle_and_zip_reuse_download_helpers_without_install(self):
        for method in ('homebrew', 'winlibs'):
            with self.subTest(method=method), patch(method + '.download') as download, \
                    patch(method + '.install') as install:
                availability.check(dict(method=method), {})
                destination = download.call_args.args[0]
                self.assertIsInstance(destination, Path)
                if method == 'winlibs':
                    download.assert_called_once_with(destination)
                else:
                    download.assert_called_once_with(destination, availability.homebrew.DIGEST)
                self.assertFalse(destination.parent.exists())
                install.assert_not_called()

    def test_timus_downloads_full_pinned_zip_and_classifies_digest_failure(self):
        name = 'timus-gcc13-cpp23'
        self.assertEqual(availability.plan([name]), [dict(
            profile=name, source=winlibs.PROVENANCE, url=winlibs.URL, method='winlibs')])
        # Multiple chunks, including a partial final chunk, must all be hashed.
        payload = b'synthetic zip bytes\x00' * 120000
        for data, status in ((payload, 'verified'), (payload + b'X', 'integrity alert'),
                             (payload[:-1], 'integrity alert')):
            stream = io.BytesIO(data)
            with self.subTest(status=status, size=len(data)), \
                    patch('winlibs.urlopen', return_value=stream) as fetch, \
                    patch('winlibs.DIGEST', hashlib.sha256(payload).hexdigest()), \
                    patch('winlibs.install') as install, patch('winlibs.extract') as extract, \
                    patch('winlibs.verify') as verify, patch('winlibs.run') as run:
                row, = availability.run([name])['results']
                self.assertEqual(row['status'], status)
                self.assertEqual(row['source'], winlibs.PROVENANCE)
                self.assertEqual(row['host'], 'github.com')
                fetch.assert_called_once_with(winlibs.URL, timeout=60)
                self.assertTrue(stream.closed)
                for operation in (install, extract, verify, run):
                    operation.assert_not_called()
        with patch('winlibs.urlopen', side_effect=TimeoutError('timeout')):
            self.assertEqual(availability.run([name])['results'][0]['status'], 'unavailable')

    def test_unsupported_winlibs_pin_and_obsolete_recipe_are_rejected(self):
        for compiler in (winlibs.PROVENANCE[:-1] + '0', 'recipe=' + winlibs.URL):
            with self.subTest(compiler=compiler), patch('availability.identity', return_value=compiler), \
                    self.assertRaisesRegex(ValueError, 'unsupported'):
                availability.plan(['timus-gcc13-cpp23'])

    def test_all_profiles_have_plans_and_exact_msys_lock(self):
        names = profiles()
        tasks = availability.plan(names)
        self.assertEqual({t['profile'] for t in tasks}, set(names))
        locked = {l for l in (availability.ROOT / 'ci/profiles/mingw-packages.txt').read_text().splitlines() if l and not l.startswith('#')}
        self.assertEqual({t['source'] for t in tasks if t['method'] == 'msys2'}, locked)
        self.assertEqual(sum(t['method'] == 'ubuntu' for t in tasks), 18)

    def test_shared_artifacts_download_once_and_integrity_classification(self):
        names = ['yukicoder-gcc16-cpp23', 'yukicoder-gcc16-gnupp23']
        with patch('availability.check') as check:
            data = availability.run(names)
        self.assertEqual(check.call_count, 1)
        self.assertEqual([r['status'] for r in data['results']], ['verified', 'verified'])
        for error, status in [(IntegrityError('bad digest'), 'integrity alert'), (TimeoutError('timeout'), 'unavailable')]:
            with patch('availability.check', side_effect=error):
                self.assertEqual(availability.run(names)['results'][0]['status'], status)


class SignedPackageTests(unittest.TestCase):
    def test_signed_hash_including_suffix_and_tampering(self):
        for algorithm in ('sha256', 'sha512'):
            fixture = json.loads((FIXTURES / ('signed-' + algorithm + '.json')).read_text())
            payload = base64.b64decode(fixture['payload'])
            sig = base64.b64decode(fixture['signature'])
            keys = {fixture['fingerprint']: (int(fixture['n']), fixture['e'])}
            digest, suffix = signed_digest(sig, keys)
            self.assertEqual(verify_stream(io.BytesIO(payload), digest, suffix=suffix), len(payload))
            for bad in (payload[:-1], payload + b'X', payload.replace(b'\r\n', b'\n')):
                with self.assertRaises(IntegrityError):
                    verify_stream(io.BytesIO(bad), digest, suffix=suffix)
            with self.assertRaises(IntegrityError):
                signed_digest(sig, {})
            with self.assertRaises((IntegrityError, ValueError)):
                signed_digest(sig[:-1] + bytes([sig[-1] ^ 1]), keys)
            with self.assertRaises(ValueError):
                signed_digest(sig[:-1], keys)
            with self.assertRaises(ValueError):
                signed_digest(sig + sig, keys)
            altered = bytearray(sig)
            altered[9] = 2 # unsupported hash algorithm (packet header is six bytes).
            with self.assertRaises(ValueError):
                signed_digest(bytes(altered), keys)

    def test_bad_packet_types_and_paths(self):
        for data in (b'no packet', b'\xcb\xe0', b'\x8b'):
            with self.assertRaises(ValueError):
                list(packets(data))
        for path in ('/absolute', '../escape', 'pool/../../escape', 'https://bad.test/x', 'pool/a?x'):
            with self.assertRaises(ValueError):
                availability.relative(path)
