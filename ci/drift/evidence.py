# SPDX-License-Identifier: GPL-3.0-or-later
"""Run declarative evidence watches; only 'same facts' is a pass."""
import importlib
import json
from pathlib import Path
import re
import tomllib

from common import ROOT, arguments, envelope, fetch, host, profiles, result, summarize, write


class ExtractorFailure(ValueError):
    """Zero/multiple matches, missing fields, malformed or unsupported source."""


class FetchFailure(ValueError):
    """HTTP 200 containing login, challenge or other non-source content."""


class EvidenceUnavailable(OSError):
    """Source cannot be obtained (403, timeout, rate limit, etc.)."""


# Keep exception identities stable when this module is executed as a script and
# an extractor imports its public interface as `from evidence import ...`.
import sys
sys.modules.setdefault('evidence', sys.modules[__name__])


def facts(value):
    strings = {'language_id', 'compiler_family', 'compiler_version'}
    lists = {'compile_arguments', 'runtime_notes', 'library_notes'}
    if not isinstance(value, dict) or set(value) not in (strings | lists, strings | lists | {'language_label'}):
        raise ExtractorFailure('facts require language ID, compiler family/version, ordered compile arguments and runtime/library notes')
    if 'language_label' in value:
        strings = strings | {'language_label'}
    if any(not isinstance(value[k], str) or not value[k] for k in strings):
        raise ExtractorFailure('missing fact string')
    if any(not isinstance(value[k], list) or any(not isinstance(x, str) for x in value[k]) for k in lists):
        raise ExtractorFailure('fact arrays must contain exact strings')
    return value


def watches(selected, directory=ROOT / 'data/judges/watch'):
    sources = []
    paths = sorted(directory.glob('*.toml'))
    # The caller's selection controls coverage; normal CI selects all shipped profiles.
    for path in paths:
        name = path.stem
        if name not in selected:
            continue
        watch = tomllib.loads(path.read_text())
        if set(watch) != {'schema', 'sources'} or watch['schema'] != 1 or not isinstance(watch['sources'], list) or not watch['sources']:
            raise ValueError('invalid watch envelope: ' + str(path))
        ids = set()
        for source in watch['sources']:
            if set(source) != {'id', 'url', 'kind', 'extractor', 'expected', 'excerpt'}:
                raise ValueError('invalid watch source fields')
            if (not isinstance(source['id'], str) or not re.fullmatch(r'[a-z0-9-]+', source['id']) or source['id'] in ids
                    or source['kind'] not in ('deployment', 'availability')
                    or not isinstance(source['url'], str) or not re.fullmatch(r'https://[^\s]+', source['url']) or not host(source['url'])
                    or not isinstance(source['extractor'], str) or not re.fullmatch(r'[a-z][a-z0-9_]*', source['extractor'])
                    or not isinstance(source['excerpt'], str) or not source['excerpt'].strip()):
                raise ValueError('invalid watch source values')
            facts(source['expected'])
            ids.add(source['id'])
            sources.append(dict(source, profile=name))
    return sources


def expected(selected):
    return {(s['profile'], s['id']): (host(s['url']), s['kind']) for s in watches(selected)}


def inspect(source, fetcher=fetch, loader=importlib.import_module):
    try:
        payload, metadata = fetcher(source['url'], limit=4 * 1024 * 1024, timeout=30)
    except Exception as error:
        return 'evidence unavailable', str(error)
    try:
        extractor = loader('extractors.' + source['extractor'])
        actual = facts(extractor.extract(payload, {**metadata, 'source_url': source['url'], 'kind': source['kind'],
                                                  'profile': source.get('profile'), 'source_id': source['id']}))
        status = 'same facts' if actual == source['expected'] else 'different facts'
        return status, json.dumps({'expected': source['expected'], 'actual': actual, 'excerpt': source['excerpt']}, ensure_ascii=True)
    except FetchFailure as error:
        return 'fetch failure', str(error)
    except EvidenceUnavailable as error:
        return 'evidence unavailable', str(error)
    except Exception as error:
        return 'extractor failure', str(error)


def run(selected):
    sources = watches(selected)
    if not sources:
        print('no sources')
    rows = []
    for source in sources:
        status, detail = inspect(source)
        rows.append(result(source['profile'], source['id'], host(source['url']), status, detail, source['kind']))
    return envelope('evidence', selected, rows)


def build_parser():
    return arguments(__doc__)


if __name__ == '__main__':
    args = build_parser().parse_args()
    data = run(profiles(args.profiles))
    write(args.output, data)
    summarize(data)
