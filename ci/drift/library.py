# SPDX-License-Identifier: GPL-3.0-or-later
"""Report weekly L1c snapshot/audit/agreement drift through the existing issue protocol."""
from pathlib import Path
from common import ROOT, arguments, envelope, profiles, result, summarize, write
from reproduction import difference


def expected(selected):
    return {(name, 'library.json'): ('', 'availability') for name in selected}


def compare(selected, artifacts, build_result, shipped=ROOT / 'data/library/library.json'):
    path = artifacts / 'library-generated/library.json'
    if not path.is_file():
        status, detail = 'missing artifact', 'incomplete L1c matrix or failed acquisition/audit; see library workflow logs'
    elif not shipped.is_file():
        status, detail = 'different', 'L1c baseline missing: review library-generated/library.json and merge by PR'
    elif path.read_bytes() != shipped.read_bytes():
        status, detail = 'different', difference('library.json', shipped.read_bytes(), path.read_bytes())
    elif build_result != 'success':
        status, detail = 'build failure', 'L1c workflow failed; matching bytes cannot override a failed matrix job'
    else:
        status, detail = 'identical', 'L1c snapshots, dependency classification and agreement identical'
    return envelope('library', selected, [result(n, 'library.json', '', status, detail) for n in selected])


def build_parser():
    parser = arguments(__doc__)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--build-result', choices=('success', 'failure'), required=True)
    return parser


if __name__ == '__main__':
    args = build_parser().parse_args()
    data = compare(profiles(args.profiles), args.artifacts, args.build_result)
    write(args.output, data)
    summarize(data)
    raise SystemExit(0 if all(r['status'] == 'identical' for r in data['results']) else 1)
