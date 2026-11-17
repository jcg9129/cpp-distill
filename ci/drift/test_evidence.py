# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import evidence
from evidence import EvidenceUnavailable, ExtractorFailure, FetchFailure, inspect, watches
from common import ROOT

FIXTURES = Path(__file__).with_name('fixtures')


def synthetic_extract(payload, metadata):
    if b'Login challenge' in payload:
        raise FetchFailure('challenge page despite HTTP 200')
    entries = json.loads(payload)['entries']
    if len(entries) != 1:
        raise ExtractorFailure('expected exactly one language entry')
    return entries[0]


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.source = dict(id='language', url='https://judge.test/languages', kind='deployment', extractor='synthetic',
                           expected=json.loads((FIXTURES / 'same.json').read_bytes())['entries'][0], excerpt='原文 GCC 15.2.0')

    def inspect_fixture(self, name):
        def fetcher(url, **kw):
            return (FIXTURES / name).read_bytes(), {'status': 200, 'url': url, 'headers': {}}
        return inspect(self.source, fetcher, lambda _: SimpleNamespace(extract=synthetic_extract))

    def test_classes_and_exact_ordered_macro_text(self):
        expected = {'same.json': 'same facts', 'changed.json': 'different facts', 'missing.json': 'extractor failure',
                    'duplicated.json': 'extractor failure', 'missing-field.json': 'extractor failure', 'challenge.html': 'fetch failure'}
        for fixture, status in expected.items():
            with self.subTest(fixture=fixture):
                self.assertEqual(self.inspect_fixture(fixture)[0], status)
        self.source['expected']['compile_arguments'][1] = '-DNAME=exact text'
        self.assertEqual(self.inspect_fixture('same.json')[0], 'different facts')

    def test_fetch_errors_and_extractor_errors(self):
        for error in [TimeoutError('timeout'), EvidenceUnavailable('403'), OSError('429')]:
            def fetcher(*a, **kw):
                raise error
            self.assertEqual(inspect(self.source, fetcher)[0], 'evidence unavailable')
        self.assertEqual(inspect(self.source, lambda *a, **kw: (b'x', {}), lambda _: SimpleNamespace(extract=lambda *a: {}))[0], 'extractor failure')
        def loader(name):
            raise ImportError('missing extractor')
        self.assertEqual(inspect(self.source, lambda *a, **kw: (b'x', {}), loader)[0], 'extractor failure')

    def test_no_sources(self):
        with patch.object(evidence, 'watches', return_value=[]), contextlib.redirect_stdout(io.StringIO()) as output:
            data = evidence.run(['p'])
        self.assertIn('no sources', output.getvalue())
        self.assertTrue(data['complete'])
        self.assertEqual(data['results'], [])

    def test_watch_schema(self):
        import copy
        def text(source):
            lines = ['schema = 1', '[[sources]]']
            for key in ('id', 'url', 'kind', 'extractor', 'excerpt'):
                lines.append(key + ' = ' + json.dumps(source[key], ensure_ascii=False))
            lines += ['[sources.expected]']
            lines += [k + ' = ' + json.dumps(v) for k, v in source['expected'].items()]
            return '\n'.join(lines)
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = Path(directory)
            path = root / 'p.toml'
            path.write_text(text(self.source))
            self.assertEqual(watches(['p'], root)[0]['expected'], self.source['expected'])
            self.assertEqual(watches(['other'], root), [])
            for key, value in [('id', '../bad'), ('extractor', '../arbitrary'), ('kind', 'adoption'), ('url', 'file:///etc/passwd'), ('excerpt', '')]:
                source = copy.deepcopy(self.source)
                source[key] = value
                path.write_text(text(source))
                with self.subTest(key=key), self.assertRaises(ValueError):
                    watches(['p'], root)
