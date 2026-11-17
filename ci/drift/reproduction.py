# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare downloaded snapshot artifacts to shipped bytes, without normalization."""
from common import ROOT, arguments, envelope, profiles, result, write

SUFFIXES = ('-predefined.txt', '-probe-expected.txt')


def expected(selected):
    return {(name, name + suffix): ('', 'availability') for name in selected for suffix in SUFFIXES}


def compare(selected, directory, shipped=ROOT / 'data/judges'):
    rows = []
    for name in selected:
        for suffix in SUFFIXES:
            filename = name + suffix
            path = directory / ('snapshot-' + name) / filename
            if not path.is_file():
                status, detail = 'missing artifact', 'missing output (including failed/skipped profile or probe): ' + filename
            elif path.read_bytes() != (shipped / filename).read_bytes():
                status, detail = 'different', 'artifact differs byte for byte: ' + filename
            else:
                status, detail = 'identical', 'byte equality: ' + filename
            rows.append(result(name, filename, '', status, detail))
    return envelope('reproduction', selected, rows)


if __name__ == '__main__':
    from pathlib import Path
    parser = arguments(__doc__)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--build-result', choices=('success', 'failure'), required=True)
    args = parser.parse_args()
    selected = profiles(args.profiles)
    data = compare(selected, args.artifacts)
    if args.build_result == 'failure':
        # Even if artifacts happened to be uploaded, a failed build/sweep/probe
        # must not become green just because bytes from another step match.
        for row in data['results']:
            if row['status'] == 'identical':
                row.update(status='build failure', detail='profile regeneration workflow failed')
    write(args.output, data)
    raise SystemExit(0 if all(r['status'] == 'identical' for r in data['results']) else 1)
