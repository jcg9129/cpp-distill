#!/usr/bin/env python3
"""Record reproducible input/header manifests and soundness evidence."""
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
from measure import lex, SPLICE


def main():
    out = Path('docs/reviews/minify-analysis')
    work = Path('measurements/minify-analysis')
    audit = json.loads((work/'renames.json').read_text())
    sources = sorted(Path('/home/appuser/.cache/caide-gap/caide2/artifacts').glob(
        'caide-compare-shard-*/examples/*/distill-compact.cpp'),key=lambda p:p.parent.name)
    headers, observations, attrs, lengths = {}, defaultdict(list), Counter(), Counter()
    identities = changed = splices = includes = duplicate_includes = once_defines = 0
    manifest = ['example\tcpp_sha256\treport_sha256']
    tracked = ['__func__','__FUNCTION__','__PRETTY_FUNCTION__','typeid','__builtin_dump_struct',
               '__LINE__','__FILE__','tuple_size','tuple_element','iterator_category','value_type',
               'get','swap','begin','end']
    for source in sources:
        name = source.parent.name
        text = source.read_bytes().decode('latin1')
        ts = lex(text)
        a = audit[name]
        assert hashlib.sha256(source.read_bytes()).hexdigest() == a['source_sha256']
        assert hashlib.sha256(source.with_suffix('.rep').read_bytes()).hexdigest() == a['report_sha256']
        manifest.append('\t'.join((name,a['source_sha256'],a['report_sha256'])))
        headers.update(a['header_sha256'])
        identities += sum(k==v['new'] for k,v in a['renames'].items())
        changed += sum(k!=v['new'] for k,v in a['renames'].items())
        lengths.update(len(v['new']) for v in a['renames'].values())
        splices += len(SPLICE.findall(text))
        report = source.with_suffix('.rep').read_text()
        for phrase in ['wrote pruned','whitespace compacted: no user code observes source positions','A6','A12','D3f']:
            if phrase in report:
                observations[phrase].append(name)
        ids = {t.text for t in ts if t.kind=='id'}
        for token in tracked:
            if token in ids:
                observations[token].append(name)
        seen = set()
        for i,t in enumerate(ts):
            if t.kind=='header' and i and ts[i-1].text=='include':
                includes += 1
                duplicate_includes += t.text in seen
                seen.add(t.text)
            if t.kind=='id' and t.text.startswith('DISTILL_ONCE_') and i and ts[i-1].text=='define':
                once_defines += 1
            if t.kind=='number' and '_' in t.text:
                observations['numeric_udl'].append([name,t.text])
            if t.kind=='id' and t.text=='operator' and i+1<len(ts) and ts[i+1].text.startswith('""'):
                observations['udl_decl'].append(name)
            if t.text=='[' and i+1<len(ts) and ts[i+1].text=='[':
                j = i+2
                while j<len(ts) and ts[j].text!=']':
                    if ts[j].kind=='id':
                        attrs[ts[j].text] += 1
                    j += 1
            if t.directive and t.text in ('##','%:%:'):
                observations['pasting'].append(name)
            if t.directive and t.text=='#' and i and ts[i-1].directive==t.directive:
                observations['stringizing'].append(name)
    result = dict(examples=len(sources),headers=len(headers),assignments_by_length=dict(lengths),
                  identity_assignments=identities,changed_spellings=changed,splices=splices,
                  includes=includes,duplicate_includes=duplicate_includes,once_defines=once_defines,
                  attributes=dict(attrs),observations=dict(observations))
    (out/'evidence.json').write_text(json.dumps(result,indent=2)+'\n')
    (out/'input-manifest.tsv').write_text('\n'.join(manifest)+'\n')
    header_manifest = ['path\tsha256\tbytes']
    gcc = str((work/'gcc13').resolve())
    for path,digest in sorted(headers.items()):
        p = Path(path)
        assert hashlib.sha256(p.read_bytes()).hexdigest() == digest
        header_manifest.append('\t'.join((path.replace(gcc,'$GCC13'),digest,str(p.stat().st_size))))
    (out/'header-manifest.tsv').write_text('\n'.join(header_manifest)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='observations'},indent=2))


if __name__ == '__main__':
    main()
