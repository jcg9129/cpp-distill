# SPDX-License-Identifier: GPL-3.0-or-later
"""Homebrew formula API: listing only, never judge adoption."""
from ._support import json_page, present, record
from evidence import ExtractorFailure


def extract(payload, metadata):
    row = json_page(payload, metadata)
    if row.get('name') != 'gcc':
        raise ExtractorFailure('missing gcc formula')
    version = present(row['versions']['stable'], 'stable version')
    bottle = row['bottle']['stable']
    revision, rebuild = row['revision'], bottle['rebuild']
    if type(revision) is not int or type(rebuild) is not int:
        raise ExtractorFailure('missing formula revision/bottle rebuild')
    digest = present(bottle['files']['x86_64_linux']['sha256'], 'Linux bottle digest')
    return record('gcc', version, [], [f'revision {revision}', f'bottle rebuild {rebuild}', 'x86_64_linux sha256:' + digest])
