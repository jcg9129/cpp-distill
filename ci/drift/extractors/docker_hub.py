# SPDX-License-Identifier: GPL-3.0-or-later
"""Docker Hub tag API. No compiler version can be inferred from an image tag."""
from urllib.parse import urlsplit
from ._support import json_page, match, one, present, record


def extract(payload, metadata):
    row = json_page(payload, metadata)
    path = urlsplit(metadata['source_url']).path
    location = match(r'/v2/repositories/([^/]+/[^/]+)/tags/([^/]+)', path, 'tag URL')
    # A single-object endpoint has no row array. Repeated JSON keys are rejected
    # by json_page; duplicate selected platform manifests are rejected here.
    name = present(row.get('name'), 'tag name')
    image = one((r for r in row['images'] if r.get('os') == 'linux' and r.get('architecture') == 'amd64'), 'linux/amd64 manifest')
    digest = present(row.get('digest'), 'index digest')
    child = present(image.get('digest'), 'manifest digest')
    return record(location[1] + ':' + name, 'not published', [],
                  ['linux/amd64', 'index ' + digest, 'manifest ' + child], family='not published')
