# SPDX-License-Identifier: GPL-3.0-or-later
from copy import deepcopy
import io
import unittest

from common import IntegrityError, envelope, result, validate, verify_stream


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.row = result('p', 's', 'example.test', 'verified')
        self.data = envelope('availability', ['p'], [self.row])
        self.data['run_id'] = '123'
        self.plan = {('p', 's'): ('example.test', 'availability')}

    def check(self, data):
        return validate(data, 'availability', ['p'], self.plan, '123')

    def test_complete_and_partial(self):
        self.check(self.data)
        data = deepcopy(self.data)
        data.update(complete=False, results=[])
        self.check(data)
        data['complete'] = True
        with self.assertRaises(ValueError):
            self.check(data)

    def test_reject_unknown_duplicate_or_missing_data(self):
        mutations = [lambda d: d.update(schema=2), lambda d: d.update(extra='x'),
                     lambda d: d.update(run_id='124'), lambda d: d.update(profiles=['q']),
                     lambda d: d.update(complete='true'), lambda d: d.update(claim='evidence'),
                     lambda d: d.update(results=d['results'] * 2), lambda d: d.update(results=[]),
                     lambda d: d['results'][0].update(host='evil.test'),
                     lambda d: d['results'][0].update(source='unknown'),
                     lambda d: d['results'][0].update(status='same facts'),
                     lambda d: d['results'][0].update(detail=['not a string']),
                     lambda d: d['results'][0].update(kind='deployment'),
                     lambda d: d.update(timestamp='2026-11-17'),
                     lambda d: d.update(window=[1900, 1])]
        for mutate in mutations:
            data = deepcopy(self.data)
            mutate(data)
            with self.subTest(data=data), self.assertRaises((ValueError, TypeError)):
                self.check(data)

    def test_full_stream_digest_and_size(self):
        import hashlib
        data = b'\x00abc\r\n' * 500000
        checksum = 'sha256:' + hashlib.sha256(data).hexdigest()
        self.assertEqual(verify_stream(io.BytesIO(data), checksum, size=len(data)), len(data))
        for payload, size in [(data[:-1], len(data)), (data + b'x', len(data)), (data, len(data) + 1)]:
            with self.assertRaises(IntegrityError):
                verify_stream(io.BytesIO(payload), checksum, size=size)
        with self.assertRaises(ValueError):
            verify_stream(io.BytesIO(data), 'md5:abcd')
