# SPDX-License-Identifier: GPL-3.0-or-later
"""Consistency, provenance, no-input-mutation, and prototype-result checks."""
from common import *
from candidates import prototype, LABELS
import csv


def main():
    data=json.loads((OUT/'analysis.json').read_text())
    candidates=json.loads((OUT/'candidates.json').read_text())
    checks=json.loads((OUT/'prototype-checks.json').read_text())
    coverage=json.loads((OUT/'coverage.json').read_text())
    assert [x['example'] for x in data]==NAMES
    assert sum(x['compact'] for x in data)==1074486
    assert sum(x['verify'] for x in data)==1732184
    assert len(coverage)==13 and all(x['compiler']=='15.2.0' for x in coverage)
    assert all(x['caide_fallback'] for x in data)
    assert all(sum(x['headers'].values())==sum(x['reasons'].values())==x['compact'] for x in data)
    assert all(not x['provenance_unmapped'] and not x['provenance_repairs'] for x in data)
    gaps=[(x['example'],g) for x in data for g in x['alignment_gaps']]
    assert {x for x,g in gaps}=={'point-location-quick','polygon-overlay-quick'} and len(gaps)==2
    assert all(g['bytes']==49 and g['text'].strip()=='detail::segment_sweep<T> sweep(segments,  true);' for _,g in gaps)
    assert sum(x['projected']<65536 for x in data)==3
    total=collections.Counter()
    for x in data:
        total.update(x['reasons'])
        assert x['headers']['example']>next(d['bytes'] for d in x['declarations'] if d['name']=='main')
        assert len(x['line_map'])==len((folder(x['example'])/'distill-compact.cpp').read_bytes().splitlines())
        for filename,digest in x['sha256'].items():
            assert sha(folder(x['example'])/filename)==digest
    assert total['name rule']==747735 and total['dependent name']==122
    by_check={(x['scenario'],x['example']):x for x in checks}
    tested=0
    for row in candidates:
        x=next(x for x in data if x['example']==row['example'])
        code,saved,_=prototype(x,row['scenario'])
        assert saved==row['saved'] and len(code)==row['compact_after']
        assert row['projected']==(len(code)*8736+9999)//10000
        if saved:
            check=by_check[row['scenario'],row['example']]
            assert check['stdout_matches'] and check['bytes']==len(code)
            assert (OUT/'prototypes'/row['scenario']/row['example']/'example.cpp').read_bytes()==code
            tested+=1
    expected={'S':5,'B':5,'W':4,'M':4,'H':5,'G':4,'T':3,'F':5,'L':4,'SBW':8,'HG':6,'LH':5,'SBWMHGT':11,'SBWMHGTF':12}
    assert {k:sum(r['under'] for r in candidates if r['scenario']==k) for k in expected}==expected
    elementary=json.loads((OUT/'elementary.json').read_text())
    assert elementary['bytes']==32461 and elementary['equivalence_cases']==260 and elementary['stdout_matches']
    for path,digest in json.loads((OUT/'inputs.json').read_text()).items():
        assert sha(Path(path))==digest,path
    # Tracked CSVs are independent exports of complete matrix rows, not top-N summaries.
    here=Path(__file__).resolve().parent
    for filename,key in [('header-by-example.csv','headers'),('reason-by-example.csv','reasons')]:
        rows=list(csv.DictReader((here/filename).open()))
        for x in data:
            assert sum(int(r[x['example']]) for r in rows)==x['compact']
            assert {r['category']:int(r[x['example']]) for r in rows if int(r[x['example']])}=={k:v for k,v in x[key].items() if v}
        assert all(sum(int(r[name]) for name in NAMES)==int(r['total']) for r in rows)
    print(f'PASS: 13 exact byte ledgers and source maps; 13 original/compact executions; {tested} compiled, stdout-matching split prototypes; 260 elementary cases; all input hashes unchanged; complete CSV matrices.')


if __name__=='__main__':
    main()
