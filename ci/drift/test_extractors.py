# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline production-format regressions: every watch and every failure class."""
import copy
import importlib
import json
from pathlib import Path
import socket
import tomllib
import unittest
from unittest.mock import patch

from common import ROOT, profiles
from evidence import facts, inspect, watches, ExtractorFailure
from extractors._support import command

FIXTURES = Path(__file__).with_name('fixtures')
WATCH = ROOT / 'data/judges/watch'
SELECTED = sorted(p.stem for p in WATCH.glob('*.toml'))
SOURCES = watches(SELECTED)


def extension(source):
    return {'library_checker': 'toml', 'homebrew': 'json', 'docker_hub': 'json'}.get(source['extractor'], 'html')


def fixture(source, variant):
    if variant == 'current':
        variant = {'pjudge': 'current-mirror', 'docker-kattis': 'current-kattis'}.get(source['id'], variant)
    if source['extractor'] == 'yukicoder' and source['profile'] == 'yukicoder-gcc16-gnupp23' and variant in ('missing', 'duplicated'):
        variant += '-gnu'
    ext = 'html' if variant in ('empty', 'challenge', 'login') else extension(source)
    return FIXTURES / source['extractor'] / (variant + '.' + ext)


class ExtractorTests(unittest.TestCase):
    def setUp(self):
        # A stray fetch in an extractor or test must fail without contacting a site.
        self.network = patch.object(socket, 'create_connection', side_effect=AssertionError('offline test attempted network'))
        self.network.start()
        self.addCleanup(self.network.stop)

    def check(self, source, data, metadata=None):
        return inspect(source, lambda url, **kw: (data, {'status': 200, 'headers': {}, **(metadata or {})}))[0]

    def test_every_watch_against_saved_pages_and_variants(self):
        classes = {'current': 'same facts', 'changed': 'different facts', 'missing': 'extractor failure',
                   'duplicated': 'extractor failure', 'challenge': 'fetch failure', 'login': 'fetch failure', 'empty': 'extractor failure'}
        for source in SOURCES:
            for variant, expected in classes.items():
                with self.subTest(profile=source['profile'], source=source['id'], variant=variant):
                    self.assertEqual(self.check(source, fixture(source, variant).read_bytes()), expected)

    def test_missing_required_fields(self):
        changes = {
            'atcoder': ('コンパイルコマンド', 'missing field'),
            'codeforces': ('g++ -Wall', 'missing -Wall'),
            'cses': ('g++ 13.3.0', ''),
            'kattis': ('g++-15 15.2.0', ''),
            'qoj': ('All C++ submissions are compiled', 'missing compiler paragraph'),
            'timus': ('<CODE CLASS="language-batch">', '<span>'),
            'yukicoder': ('<td>./a.out</td>', '<td></td>'),
            'library_checker': ('    name = "C++23"', '    missing_name = "C++23"'),
            'homebrew': ('"revision":0', '"missing_revision":0'),
            'docker_hub': ('"digest":', '"missing_digest":'),
        }
        seen = set()
        for source in SOURCES:
            name = source['extractor']
            if name in seen:
                continue
            seen.add(name)
            old, new = changes[name]
            text = fixture(source, 'current').read_text()
            self.assertIn(old, text)
            with self.subTest(extractor=name):
                self.assertEqual(self.check(source, text.replace(old, new, 1).encode()), 'extractor failure')

    def test_200_consent_challenge_and_rate_limit_all_formats(self):
        for source in SOURCES:
            for data, status in [
                (b'<title>Consent required</title><form id="consent-form">Agree</form>', 'fetch failure'),
                (b'<title>Attention Required! | Cloudflare</title>', 'fetch failure'),
                (b'<title>Too Many Requests</title>', 'evidence unavailable'),
            ]:
                with self.subTest(source=source['id'], data=data):
                    self.assertEqual(self.check(source, data), status)
            for status in (403, 429, 503):
                with self.subTest(source=source['id'], http=status):
                    self.assertEqual(self.check(source, b'', {'status': status}), 'evidence unavailable')

    def test_transport_unavailable_all_formats(self):
        for source in SOURCES:
            for error in (TimeoutError('slow source'), OSError('HTTP 403'), OSError('HTTP 429')):
                def unavailable(*args, **kwargs):
                    raise error
                with self.subTest(source=source['id'], error=str(error)):
                    self.assertEqual(inspect(source, unavailable)[0], 'evidence unavailable')

    def test_exact_macro_spelling_order_and_duplicate_arguments(self):
        self.assertEqual(command('g++ -DFOO="two  spaces" -DNAME= -pthread -pthread'),
                         ['g++', '-DFOO="two  spaces"', '-DNAME=', '-pthread', '-pthread'])
        atcoder = next(s for s in SOURCES if s['extractor'] == 'atcoder')
        args = atcoder['expected']['compile_arguments']
        self.assertEqual(args.count('-pthread'), 2)
        for mutation in ('quote', 'order', 'duplicate'):
            source = copy.deepcopy(atcoder)
            argv = source['expected']['compile_arguments']
            if mutation == 'quote':
                argv[argv.index('-DOR_PROTO_DLL=')] = '-DOR_PROTO_DLL=""'
            elif mutation == 'order':
                argv[4], argv[5] = argv[5], argv[4]
            else:
                argv.remove('-pthread')
            with self.subTest(mutation=mutation):
                self.assertEqual(self.check(source, fixture(atcoder, 'current').read_bytes()), 'different facts')

    def test_display_labels_are_independent_facts(self):
        source = next(s for s in SOURCES if s['extractor'] == 'library_checker')
        text = fixture(source, 'current').read_text().replace('name = "C++23"', 'name = "C++23 renamed"', 1)
        self.assertEqual(self.check(source, text.encode()), 'different facts')
        legacy = {k: v for k, v in source['expected'].items() if k != 'language_label'}
        facts(legacy)  # Part 1's six-field facts remain supported.
        for bad in ('', [], None):
            with self.subTest(bad=bad), self.assertRaises(ExtractorFailure):
                facts({**legacy, 'language_label': bad})

    def test_literal_array_macro_and_missing_runtime(self):
        source = copy.deepcopy(next(s for s in SOURCES if s['extractor'] == 'atcoder'))
        text = fixture(source, 'current').read_text()
        macro = '-DNAME="two  spaces"'
        source['expected']['compile_arguments'][5] = macro
        text = text.replace('"-DNOMINMAX"', json.dumps(macro), 1)
        self.assertEqual(self.check(source, text.encode()), 'same facts')
        self.assertEqual(self.check(source, text.replace('./a.out', '', 1).encode()), 'extractor failure')

    def test_presentation_whitespace_and_original_language(self):
        for source in SOURCES:
            if source['extractor'] not in ('atcoder', 'yukicoder', 'timus', 'qoj'):
                continue
            data = fixture(source, 'current').read_text().replace('\n', '\r\n').encode('utf-8-sig')
            with self.subTest(source=source['id'], profile=source['profile']):
                self.assertEqual(self.check(source, data), 'same facts')
        timus = next(s for s in SOURCES if s['extractor'] == 'timus')
        text = fixture(timus, 'current').read_text()
        self.assertIn('Компиляторы запускаются', text)
        self.assertEqual(self.check(timus, text.encode('cp1251'), {'headers': {'Content-Type': 'text/html; charset=windows-1251'}}), 'same facts')

    def test_watch_coverage_and_reviewed_profile_facts(self):
        self.assertEqual(set(SELECTED), set(profiles()))
        self.assertEqual({s['profile'] for s in watches(profiles())}, set(profiles()))
        for path in (ROOT / 'data/judges').glob('*.toml'):
            profile = tomllib.loads(path.read_text())
            sources = [s for s in SOURCES if s['profile'] == path.stem and s['kind'] == 'deployment']
            self.assertTrue(sources, path.stem)
            for source in sources:
                self.assertEqual(source['expected']['compiler_family'], profile['compiler']['family'])
                self.assertEqual(source['expected']['compiler_version'], profile['compiler']['version'])
                args = source['expected']['compile_arguments']
                for arg in profile['flags'] + profile['link']:
                    self.assertIn(arg, args)
                self.assertIn('-std=' + profile['std'] if source['extractor'] != 'qoj' else '-std=c++XX', args)
                self.assertNotIn('/blob/01fa3c', source['url'])
