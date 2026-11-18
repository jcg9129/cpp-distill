# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared result protocol and bounded, streaming HTTP/digest operations."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'ci/profiles'))
CLAIMS = ('evidence', 'availability', 'reproduction', 'library')
STATUSES = {
    'evidence': {'same facts', 'different facts', 'extractor failure', 'fetch failure', 'evidence unavailable'},
    'availability': {'verified', 'unavailable', 'integrity alert'},
    'reproduction': {'identical', 'different', 'missing artifact', 'build failure'},
    'library': {'identical', 'different', 'missing artifact', 'build failure'},
}
PASSES = {'same facts', 'verified', 'identical'}


class IntegrityError(ValueError):
    pass


def host(url):
    return urlsplit(url).hostname or ''


def fetch(url, *, headers=None, limit=16 * 1024 * 1024, timeout=60):
    with urlopen(Request(url, headers={'User-Agent': 'cpp-distill-drift/1', **(headers or {})}), timeout=timeout) as response:
        data = response.read(limit + 1)
        if len(data) > limit:
            raise ValueError('HTTP metadata exceeds size budget')
        return data, {'url': response.url, 'status': response.status, 'headers': dict(response.headers)}


def verify_stream(stream, digest, *, size=None, suffix=b''):
    algorithm, value = digest.split(':', 1)
    if algorithm not in ('sha256', 'sha512') or not re.fullmatch('[0-9a-f]{%d}' % (64 if algorithm == 'sha256' else 128), value):
        raise ValueError('invalid expected digest')
    checksum = hashlib.new(algorithm)
    count = 0
    while chunk := stream.read(1024 * 1024):
        checksum.update(chunk)
        count += len(chunk)
    checksum.update(suffix)
    if checksum.hexdigest() != value or (size is not None and count != size):
        raise IntegrityError('downloaded bytes differ from expected digest or size')
    return count


def download(url, digest, *, headers=None, size=None, suffix=b''):
    with urlopen(Request(url, headers=headers or {}), timeout=120) as response:
        return verify_stream(response, digest, size=size, suffix=suffix)


def profiles(selection='all'):
    from matrix import matrix
    return [row['profile'] for row in matrix(selection)['include']]


def arguments(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument('--profiles', default='all')
    parser.add_argument('--output', type=Path, required=True)
    return parser


def result(profile, source, host_name, status, detail='', kind='availability'):
    return dict(profile=profile, source=source, host=host_name, status=status, detail=str(detail)[:4000], kind=kind)


def envelope(claim, selected, rows, *, complete=True):
    now = datetime.now(timezone.utc)
    return dict(schema=1, claim=claim, profiles=selected, run_id=os.environ.get('GITHUB_RUN_ID', '0'),
                timestamp=now.isoformat(), window=list(now.date().isocalendar()[:2]),
                complete=complete, results=rows)


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=True, indent=2) + '\n')


def summarize(data):
    """Keep classified failures visible in job logs as well as JSON artifacts."""
    for row in data['results']:
        print(f"{row['profile']}: {row['source']}: {row['status']}")
        if row['status'] not in PASSES:
            print(row['detail'])


def validate(data, claim, selected, expected, run_id):
    """Validate the entire artifact before any issue mutation. expected is a trusted plan."""
    keys = {'schema', 'claim', 'profiles', 'run_id', 'timestamp', 'window', 'complete', 'results'}
    if not isinstance(data, dict) or set(data) != keys or data['schema'] != 1 or data['claim'] != claim:
        raise ValueError('invalid result envelope')
    if data['profiles'] != selected or data['run_id'] != run_id or type(data['complete']) is not bool:
        raise ValueError('result selection/run differs from plan')
    timestamp = datetime.fromisoformat(data['timestamp'])
    if timestamp.tzinfo is None or list(timestamp.date().isocalendar()[:2]) != data['window']:
        raise ValueError('invalid result timestamp/window')
    if not isinstance(data['results'], list):
        raise ValueError('results must be an array')
    seen = set()
    for row in data['results']:
        if not isinstance(row, dict) or set(row) != {'profile', 'source', 'host', 'status', 'detail', 'kind'}:
            raise ValueError('invalid result row')
        if any(not isinstance(v, str) or len(v) > 16000 for v in row.values()):
            raise ValueError('invalid row field')
        key = (row['profile'], row['source'])
        if key in seen or key not in expected or (row['host'], row['kind']) != expected[key]:
            raise ValueError('duplicate or unexpected source/host/kind')
        if row['status'] not in STATUSES[claim]:
            raise ValueError('invalid result class')
        seen.add(key)
    if data['complete'] and seen != expected.keys():
        raise ValueError('partial result coverage')
    return data
