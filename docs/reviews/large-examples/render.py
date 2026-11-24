# SPDX-License-Identifier: GPL-3.0-or-later
"""Export complete, reviewable byte tables; Markdown fragments stay ignored."""
from common import *
from candidates import LABELS
import csv

HERE = Path(__file__).resolve().parent


def write_csv(name, fields, rows):
    with (HERE/name).open('w',newline='') as stream:
        writer = csv.DictWriter(stream,fieldnames=fields,lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def table(fields, rows):
    return '\n'.join(['| '+' | '.join(fields)+' |', '| '+' | '.join(['---']*len(fields))+' |']+
                     ['| '+' | '.join(str(v) for v in row)+' |' for row in rows])+'\n'


def main():
    data = json.loads((OUT/'analysis.json').read_text())
    profiles = {x['example']:x for x in json.loads((OUT/'coverage.json').read_text())}
    candidates = json.loads((OUT/'candidates.json').read_text())
    headers, reasons = collections.Counter(), collections.Counter()
    for x in data:
        headers.update(x['headers'])
        reasons.update(x['reasons'])
    for name,key,total in [('header-by-example.csv','headers',headers),('reason-by-example.csv','reasons',reasons)]:
        rows = [dict(category=k,**{x['example']:x[key].get(k,0) for x in data},total=total[k])
                for k in sorted(total,key=lambda k:(-total[k],k))]
        write_csv(name,['category']+NAMES+['total'],rows)
    scenario_rows = [{k:r[k] for k in ['scenario','example','saved','compact_after','projected','under']} for r in candidates]
    write_csv('scenarios.csv',['scenario','example','saved','compact_after','projected','under'],scenario_rows)
    coverage, declarations, witnesses = [], [], []
    witness_names = {'atl::detail::write_uint128','atl::detail::read_stream_magnitude','atl::static_vector',
                     'atl::convolution_uint64','atl::detail::fft_split_convolution',
                     'atl::detail::inverse','atl::detail::hash_float_value',
                     'atl::geometry::detail::adaptive_cross_tail','atl::detail::forest_edge_registry',
                     'atl::detail::composition_brent_kung'}
    for x in data:
        buf = (folder(x['example'])/'distill-compact.cpp').read_bytes()
        raw = {r['id']:r for r in x['raw']}
        counts = collections.Counter()
        for d in x['declarations']:
            state = 'non-function ownership'
            if d['kind'] in {'Function','Method'}:
                lo = buf[:d['start']].count(b'\n')+1
                hi = buf[:d['end']].count(b'\n')+1
                funcs = [f for f in profiles[x['example']]['functions'] if lo<=f['start_line']<=hi and f['end_line']<=hi]
                state = 'observed executed' if any(f['execution_count'] for f in funcs) else 'emitted but zero' if funcs else 'no runtime instance in coverage'
                counts[state] += d['bytes']
            row = dict(example=x['example'],name=d['name'],kind=d['kind'],bytes=d['bytes'],
                       file=d['report']['file'],source_line=d['report']['source_line'],group=d['group'],
                       reason=effective(raw,d['report'])['reason'],coverage=state,
                       chain=' <- '.join(str(c['id'])+': '+c['label']+' ['+c['reason']+']' for c in d['chain']))
            declarations.append(row)
            if d['name'] in witness_names:
                witnesses.append(row)
        coverage.append(dict(example=x['example'],**{k:counts[k] for k in ['observed executed','emitted but zero','no runtime instance in coverage']}))
    write_csv('coverage-summary.csv',list(coverage[0]),coverage)
    write_csv('witnesses.csv',list(witnesses[0]),witnesses)
    # Full declaration occurrence data can be large; retain it in measurements.
    with (OUT/'declarations.csv').open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(declarations[0]),lineterminator='\n')
        writer.writeheader();writer.writerows(declarations)
    manifest = {}
    for x in data:
        for filename,digest in x['sha256'].items():
            manifest[str(folder(x['example'])/filename)] = digest
        for header in x['headers']:
            path = example_path(x['example']) if header=='example' else ATL/'include'/header
            manifest[str(path)] = sha(path)
    dump('inputs.json',manifest)
    (HERE/'inputs.sha256').write_text(''.join(f'{digest}  {name}\n' for name,digest in sorted(manifest.items())))
    fragments = []
    fragments.append(table(['Example','Compact B','Projected B','Additional compact B to remove','Caide verify B (fallback)'],
                           [[x['example'],f"{x['compact']:,}",f"{x['projected']:,}",f"{max(0,x['compact']-75017):,}",f"{x['verify']:,}"] for x in data]))
    fragments.append(table(['Immediate reason group','Compact B','Percent'],[[k,f'{v:,}',f'{100*v/sum(headers.values()):.2f}%'] for k,v in reasons.most_common()]))
    fragments.append(table(['Header','N examples','Compact B'],[[k,sum(k in x['headers'] for x in data),f'{v:,}'] for k,v in headers.most_common()]))
    fragments.append(table(['Scenario','Saving per affected example B','Aggregate saving B','Under limit / 13','Newly under'],[
        [scenario,', '.join(str(v) for v in sorted({r['saved'] for r in candidates if r['scenario']==scenario and r['saved']})),
         f"{sum(r['saved'] for r in candidates if r['scenario']==scenario):,}",sum(r['under'] for r in candidates if r['scenario']==scenario),
         ', '.join(r['example'] for r in candidates if r['scenario']==scenario and r['under'] and next(x for x in data if x['example']==r['example'])['projected']>=65536)]
        for scenario in list(LABELS)+['SBW','HG','LH','SBWMHGT','SBWMHGTF']]))
    fragments.append(table(['Example','After combined splits B','Projected B'],[[r['example'],f"{r['compact_after']:,}",f"{r['projected']:,}"] for r in candidates if r['scenario']=='SBWMHGTF']))
    fragments.append(table(['Example','Executed function-source B','Emitted, zero executions B','No runtime instance B'],[[r['example']]+[f"{r[k]:,}" for k in list(r)[1:]] for r in coverage]))
    fragments.append(table(['Example','Own code/includes B','Three largest ATL headers (compact B)','Other ATL B'],[
        [x['example'],f"{x['headers']['example']:,}",'; '.join('`'+h.removeprefix('atl/')+'` '+f'{v:,}' for h,v in sorted([(h,v) for h,v in x['headers'].items() if h!='example'],key=lambda p:-p[1])[:3]),
         f"{x['compact']-x['headers']['example']-sum(v for h,v in sorted([(h,v) for h,v in x['headers'].items() if h!='example'],key=lambda p:-p[1])[:3]):,}"] for x in data]))
    (OUT/'tables.md').write_text('\n\n'.join(fragments))
    report=HERE.parent/'2026-11-24-large-examples.md'
    if report.exists():
        text=report.read_text()
        for key,fragment in zip(['baseline','reasons','headers','scenarios','bundle','coverage','per-header'],fragments):
            pattern=r'<!-- BEGIN '+key+r' -->[\s\S]*?<!-- END '+key+r' -->'
            text,n=re.subn(pattern,lambda m:'<!-- BEGIN '+key+' -->\n'+fragment+'<!-- END '+key+' -->',text)
            assert n==1,(key,n)
        report.write_text(text)
    print('Exported',len(headers),'header categories;',len(reasons),'reason groups;',len(declarations),'written family occurrences;',len(witnesses),'witnesses;',len(manifest),'input hashes.')


if __name__=='__main__':
    main()
