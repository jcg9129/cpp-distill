# SPDX-License-Identifier: GPL-3.0-or-later
"""Kattis language page compiler paragraph and copyable command."""
from ._support import command, html, match, one, record
from evidence import ExtractorFailure


def extract(payload, metadata):
    root = html(payload, metadata)
    label = one((n for n in root.all('h1') if n.attrs.get('class') == 'programming_language-name'), 'language label').text()
    paragraph = one((n for n in root.all('p') if n.text().startswith('We use ')), 'compiler paragraph')
    version = match(r'g\+\+-\d+ ([\d.]+)', one(paragraph.all('code'), 'compiler version').text(), 'GCC version')[1]
    node = one((n for n in root.all('code') if 'data-copy-text' in n.attrs), 'compile command')
    text = node.attrs['data-copy-text']
    if node.text() != '$ ' + ' '.join(text.split()):
        raise ExtractorFailure('visible command and copy text disagree')
    return record('cpp', version, command(text), label=label)
