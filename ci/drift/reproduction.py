# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare downloaded snapshot artifacts to shipped bytes, without normalization."""
from difflib import unified_diff
from itertools import islice, zip_longest
from pathlib import Path

from common import ROOT, arguments, envelope, profiles, result, summarize, write

SUFFIXES = ('-predefined.txt', '-probe-expected.txt')


def expected(selected):
    return {(name, name + suffix): ('', 'availability') for name in selected for suffix in SUFFIXES}


def difference(filename, shipped, actual):
    before, after = shipped.splitlines(keepends=True), actual.splitlines(keepends=True)
    first = next(i for i, (a, b) in enumerate(zip_longest(before, after), 1) if a != b)
    # repr preserves CRLF/final-newline/encoding differences in the log. The
    # comparison above remains raw byte equality, with no normalization.
    diff = unified_diff([repr(line) + '\n' for line in before],
                        [repr(line) + '\n' for line in after],
                        fromfile='shipped/' + filename, tofile='artifact/' + filename, n=2)
    lines = list(islice(diff, 41))
    excerpt = ''.join(lines[:40])
    if len(lines) > 40:
        excerpt += '... diff truncated at 40 lines\n'
    return f'artifact differs byte for byte: {filename}; first differing line: {first}\n' + excerpt


def compare(selected, directory, shipped=ROOT / 'data/judges'):
    rows = []
    for name in selected:
        for suffix in SUFFIXES:
            filename = name + suffix
            path = directory / ('snapshot-' + name) / filename
            if not path.is_file():
                status, detail = 'missing artifact', 'missing output (including failed/skipped profile or probe): ' + filename
            elif path.read_bytes() != (shipped / filename).read_bytes():
                status = 'different'
                detail = difference(filename, (shipped / filename).read_bytes(), path.read_bytes())
            else:
                status, detail = 'identical', 'byte equality: ' + filename
            rows.append(result(name, filename, '', status, detail))
    return envelope('reproduction', selected, rows)


def build_parser():
    parser = arguments(__doc__)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--build-result', choices=('success', 'failure'), required=True)
    return parser


if __name__ == '__main__':
    args = build_parser().parse_args()
    selected = profiles(args.profiles)
    data = compare(selected, args.artifacts)
    if args.build_result == 'failure':
        # Even if artifacts happened to be uploaded, a failed build/sweep/probe
        # must not become green just because bytes from another step match.
        for row in data['results']:
            if row['status'] == 'identical':
                row.update(status='build failure', detail='profile regeneration workflow failed')
    write(args.output, data)
    summarize(data)
    raise SystemExit(0 if all(r['status'] == 'identical' for r in data['results']) else 1)
