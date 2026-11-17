# SPDX-License-Identifier: GPL-3.0-or-later
"""Codeforces announcement body; never read commands from user comments."""
from ._support import command, gcc_version, html, match, one, record


def extract(payload, metadata):
    root = html(payload, metadata)
    topic = one((n for n in root.all('div') if 'topic' in n.attrs.get('class', '').split()
                 and any(a.attrs.get('href') == '/blog/entry/133046' for a in n.all('a'))), 'announcement')
    title = one((n for n in topic.children if getattr(n, 'tag', '') == 'div'
                 and n.attrs.get('class') == 'title'), 'title').text()
    label = match(r'Welcome (GNU G\+\+23 ([\d.]+) \(64 bit, msys2\))', title, 'compiler label')
    content = one((n for n in topic.children if getattr(n, 'tag', '') == 'div'
                   and n.attrs.get('class') == 'content'), 'announcement content')
    body = one((n for n in content.all('div') if n.attrs.get('class') == 'ttypography'), 'announcement body')
    line = one((n.raw() for n in body.all('code') if n.raw().startswith('g++ ')), 'compile command')
    return record(label[1], gcc_version(label[2]), command(line), label=label[1])
