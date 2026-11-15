# SPDX-License-Identifier: GPL-3.0-or-later
"""Union the compatibility jobs and the checked-in lists for review in one PR."""
import argparse
from datetime import date
from pathlib import Path
from generate import LIBRARIES, ROOT, VERSION, read_list, reserved, standard


def merge(artifacts, output):
    output.mkdir(parents=True, exist_ok=True)
    standard_names, _ = read_list(ROOT / 'data/names/standard-macros.txt')
    for library in LIBRARIES:
        paths = [ROOT / 'data/extensions' / (library + '.txt'), *sorted(artifacts.rglob(library + '.txt'))]
        names, provenance = set(), set()
        for path in paths:
            extra, comments = read_list(path)
            names.update(extra)
            provenance.update(c for c in comments if c.startswith('# Scanned:'))
        names = {n for n in names if not reserved(n) and not standard(n, standard_names)}
        header = ['# SPDX-License-Identifier: GPL-3.0-or-later',
                  f'# A1 {library}; generator ci/extensions/generate.py v{VERSION}; merged {date.today().isoformat()}',
                  *sorted(provenance)]
        if not names:
            header.append('# Not yet generated; protection unavailable.')
        (output / (library + '.txt')).write_text('\n'.join(header + sorted(names)) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    merge(args.artifacts, args.output)
