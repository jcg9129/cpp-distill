#!/usr/bin/env python3
"""Compile each counterfactual with GCC 15 and the downloaded GCC 13 headers."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--jobs', type=int, default=3)
    p.add_argument('--variants', nargs='+', default=['t1','t2','both','all_rules'])
    args = p.parse_args()
    work = Path('measurements/minify-analysis')
    includes = (work / 'gcc13/usr/include').resolve()
    flags = ['g++','-std=c++23','-fsyntax-only','-nostdinc++',
             '-isystem',str(includes/'c++/13'),
             '-isystem',str(includes/'aarch64-linux-gnu/c++/13'),
             '-isystem',str(includes/'c++/13/backward')]
    logdir = work / 'compile-logs'
    logdir.mkdir(exist_ok=True)
    paths = [d/(v+'.cpp') for d in sorted((work/'outputs').iterdir()) for v in args.variants]
    def check(path):
        start = time.monotonic()
        done = subprocess.run(flags+[str(path)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
        logfile = logdir/(path.parent.name+'.'+path.stem+'.log')
        logfile.write_bytes(done.stdout)
        return dict(example=path.parent.name,variant=path.stem,exit=done.returncode,
                    sha256=hashlib.sha256(path.read_bytes()).hexdigest(),seconds=round(time.monotonic()-start,3))
    rows = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        for n, row in enumerate(executor.map(check, paths),1):
            rows.append(row)
            print(f'{n}/{len(paths)} {row["example"]} {row["variant"]}: {row["exit"]}',flush=True)
            (work/'compile-partial.json').write_text(json.dumps(rows,indent=2)+'\n')
    result = dict(checks=len(rows),passed=sum(r['exit']==0 for r in rows),
                  failed=[r for r in rows if r['exit']],rows=rows)
    Path('docs/reviews/minify-analysis/verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='rows'},indent=2))
    if result['failed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
