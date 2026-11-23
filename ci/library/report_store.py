# SPDX-License-Identifier: GPL-3.0-or-later
"""Disk-backed per-root evidence for merging large capture matrices.

Only one root is decoded at a time. The remaining report metadata is small;
canonical report chunks preserve the evidence digest of an in-memory report.
"""
from collections.abc import MutableMapping
import hashlib
import json
import sqlite3


def canonical(data):
    return json.dumps(data, sort_keys=True, separators=(',', ':'), ensure_ascii=True)


class Roots(MutableMapping):
    def __init__(self, db, report):
        self.db, self.report = db, report

    def __iter__(self):
        return iter([r[0] for r in self.db.execute(
            'SELECT header FROM roots WHERE report=? ORDER BY header', (self.report,))])

    def __len__(self):
        return self.db.execute('SELECT count(*) FROM roots WHERE report=?', (self.report,)).fetchone()[0]

    def __getitem__(self, header):
        row = self.db.execute('SELECT value FROM roots WHERE report=? AND header=?',
                              (self.report, header)).fetchone()
        if row is None:
            raise KeyError(header)
        return json.loads(row[0])

    def __setitem__(self, header, value):
        self.db.execute('INSERT OR REPLACE INTO roots VALUES (?, ?, ?)',
                        (self.report, header, canonical(value)))

    def __delitem__(self, header):
        if not self.db.execute('DELETE FROM roots WHERE report=? AND header=?',
                               (self.report, header)).rowcount:
            raise KeyError(header)

    def encoded_items(self):
        # Projection already stored canonical JSON. Hashing/writing evidence
        # must not decode and re-encode gigabytes of unchanged root records.
        yield from self.db.execute('SELECT header, value FROM roots WHERE report=? ORDER BY header',
                                   (self.report,))


class ReportStore:
    def __init__(self, path):
        self.db = sqlite3.connect(path)
        # Bound the page cache; scratch data is disposable and need not be journalled.
        self.db.execute('PRAGMA cache_size=-8192')
        self.db.execute('PRAGMA journal_mode=OFF')
        self.db.execute('CREATE TABLE roots (report INTEGER, header TEXT, value TEXT, '
                        'PRIMARY KEY (report, header))')

    def close(self):
        self.db.close()

    def load(self, path, index):
        import ijson
        from ijson.common import ObjectBuilder
        report, root, header, depth = ObjectBuilder(), None, None, 0
        roots = Roots(self.db, index)
        with path.open('rb') as source:
            for prefix, event, value in ijson.parse(source, use_float=True):
                if root is not None:
                    root.event(event, value)
                    depth += (event in ('start_map', 'start_array')) - (event in ('end_map', 'end_array'))
                    if depth == 0:
                        roots[header] = root.value
                        root = None
                elif prefix == 'audit.roots' and event == 'map_key':
                    header = value
                    root = ObjectBuilder()
                    # The next event starts the root object.
                else:
                    report.event(event, value)
        result = report.value
        result['audit']['roots'] = roots
        self.db.commit()
        return result


def report_chunks(report):
    """Exactly canonical(report), with only one disk-backed root resident."""
    yield '{'
    for i, (key, value) in enumerate(sorted(report.items())):
        if i:
            yield ','
        yield canonical(key) + ':'
        if key != 'audit':
            yield canonical(value)
            continue
        yield '{'
        for j, (field, entry) in enumerate(sorted(value.items())):
            if j:
                yield ','
            yield canonical(field) + ':'
            if field != 'roots':
                yield canonical(entry)
                continue
            yield '{'
            records = (entry.encoded_items() if isinstance(entry, Roots) else
                       ((header, canonical(entry[header])) for header in sorted(entry)))
            for k, (header, encoded) in enumerate(records):
                if k:
                    yield ','
                yield canonical(header) + ':' + encoded
            yield '}'
        yield '}'
    yield '}'


def report_digest(report):
    digest = hashlib.sha256()
    for chunk in report_chunks(report):
        digest.update(chunk.encode('utf-8'))
    return digest.hexdigest()


def write_evidence(path, reports, version):
    with path.open('w', encoding='utf-8', newline='\n') as output:
        output.write('{"builds":{')
        for i, report in enumerate(sorted(reports, key=lambda r: r['build'])):
            if i:
                output.write(',')
            output.write(canonical(report['build']) + ':')
            output.writelines(report_chunks(report))
        output.write('},"generator":' + str(version) + ',"schema":2}\n')
