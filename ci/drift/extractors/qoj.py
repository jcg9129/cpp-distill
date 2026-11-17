# SPDX-License-Identifier: GPL-3.0-or-later
"""QOJ/PJudge blog article headings, paragraphs and code blocks."""
from ._support import command, html, match, one, record, section
from evidence import ExtractorFailure


def extract(payload, metadata):
    root = html(payload, metadata)
    article = one(root.all('article'), 'environment article')
    heading, cpp = section(article, 'h4', 'C++ (')
    standards = heading.text().removeprefix('C++ (').removesuffix(')').split(', ')
    if 'C++23' not in standards:
        raise ExtractorFailure('C++23 not in supported standards')
    paragraph = one((n.text() for n in cpp.all('p') if n.text().startswith('All C++ submissions ')), 'compiler paragraph')
    version = match(r'All C\+\+ submissions are compiled using GCC ([\d.]+)\. The detailed compilation tag is as follows:', paragraph, 'GCC version')[1]
    code = one(cpp.all('pre'), 'C++ compile block')
    args = command(one(code.all('code'), 'C++ command').raw())
    # Keep the published parameter rather than inventing a literal command.
    # Both the template and the supported C++23 selection are watched.
    runtime = [one((n.text() for n in article.all('li') if n.text().startswith(prefix)), prefix)
               for prefix in ('OS:', 'CPU:')]
    return record('C++23', version, args, runtime, label='C++23')
