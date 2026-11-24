#!/usr/bin/env python3
"""Check the counterfactuals against libclang's independent raw lexer."""
import json
from pathlib import Path
from clang_inventory import raw_tokens
from measure import lex


def main():
    work = Path('measurements/minify-analysis')
    audit = json.loads((work/'renames.json').read_text())
    sources = sorted(Path('/home/appuser/.cache/caide-gap/caide2/artifacts').glob(
        'caide-compare-shard-*/examples/*/distill-compact.cpp'),key=lambda p:p.parent.name)
    rows = []
    for n, source in enumerate(sources,1):
        name = source.parent.name
        with_offsets = raw_tokens(source.resolve(),offsets=True)
        before = [(k,s) for k,s,_ in with_offsets]
        mapping = {k:v['new'] for k,v in audit[name]['renames'].items()}
        identifiers = {t.start for t in lex(source.read_bytes().decode('latin1')) if t.kind=='id'}
        # Clang's context-free raw lexer splits <version> into three tokens;
        # header-name components must not be treated as renameable identifiers.
        expected = [(k,mapping.get(s,s) if offset in identifiers else s) for k,s,offset in with_offsets]
        output = work/'outputs'/name
        checks = {}
        for variant, target in [('t1',before),('t2',expected),('both',expected)]:
            actual = raw_tokens((output/(variant+'.cpp')).resolve())
            checks[variant] = actual == target
            if actual != target:
                first = next((i for i,(x,y) in enumerate(zip(actual,target)) if x!=y),min(len(actual),len(target)))
                raise AssertionError((name,variant,first,actual[first:first+3],target[first:first+3]))
        rows.append(dict(example=name,tokens=len(before),checks=checks))
        print(f'{n}/{len(sources)} {name}: Clang tokens match',flush=True)
    result = dict(examples=len(rows),checks=sum(len(r['checks']) for r in rows),
                  passed=sum(sum(r['checks'].values()) for r in rows),rows=rows)
    Path('docs/reviews/minify-analysis/token-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'{result["passed"]}/{result["checks"]} passed')


if __name__ == '__main__':
    main()
