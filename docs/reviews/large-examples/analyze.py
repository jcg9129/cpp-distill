# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure compact bytes, retaining witnesses, and original-source provenance.

Run from the worktree root: python3 -B docs/reviews/large-examples/analyze.py
No compiler, production executable, or input-tree mutation is involved.
"""
from common import *
import difflib


def without_comments(text):
    # Preserve quoted contents and newlines. Source lines are used only to recover
    # provenance; every measured byte comes from the actual compact artifact.
    token = re.compile(r'R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')
    return token.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]) if m[0].startswith(('//', '/*')) else m[0], text)


def flattened(example):
    """Original include order, retaining both conditional arms for alignment."""
    seen, lines, origins = set(), [], []
    def walk(path):
        if path in seen:
            return
        seen.add(path)
        header = str(path.relative_to(ATL / 'include')) if path.is_relative_to(ATL / 'include') else 'example'
        for lineno, line in enumerate(without_comments(path.read_text()).splitlines(), 1):
            match = re.match(r'\s*#\s*include\s*[<"](atl/[^>"]+)[>"]', line)
            if match:
                # Every artifact explicitly selects native int128. Traversing
                # its inactive software include would mark shared dependencies
                # seen too early and corrupt their subsequent source order.
                if match[1] == 'atl/types/int128.hpp':
                    continue
                walk(ATL / 'include' / match[1])
            elif line.strip():
                lines.append('@once' if re.match(r'\s*#\s*pragma\s+once\b', line) else line.strip())
                origins.append((header, lineno))
    walk(example_path(example))
    return lines, origins


def provenance(example, buf, ds):
    src, origins = flattened(example)
    lines = buf.splitlines(keepends=True)
    keys = ['@once' if re.match(rb'#define DISTILL_ONCE_', line) else line.decode().strip() for line in lines]
    mapping = [None] * len(lines)
    for block in difflib.SequenceMatcher(None, src, keys, autojunk=False).get_matching_blocks():
        for j in range(block.size):
            mapping[block.b + j] = origins[block.a + j]
    alignment_gaps = [dict(line=i+1,text=line.decode(),bytes=len(line))
                      for i,line in enumerate(lines) if mapping[i] is None]
    # Check mapped family lines against report provenance. Generic braces and
    # repeated declarations may align to another header across a deleted region;
    # the separately reported family is authoritative for its entire span.
    pos, repairs, unmapped = 0, collections.Counter(), []
    for i, line in enumerate(lines):
        covering = [d for d in ds if d['start'] < pos + len(line) and d['end'] > pos]
        if covering:
            inner = min(covering, key=lambda d: d['end'] - d['start'])
            header = inner['report']['file']
            if mapping[i] and mapping[i][0] != header:
                repairs[(mapping[i][0], header)] += len(line)
            mapping[i] = (header, None)
        if mapping[i] is None:
            unmapped.append(dict(line=i + 1, text=line.decode(), bytes=len(line)))
        pos += len(line)
    by_header = collections.Counter()
    for line, origin in zip(lines, mapping):
        by_header[origin[0] if origin else 'unmapped structural'] += len(line)
    return dict(by_header), unmapped, [{'from': a, 'to': b, 'bytes': n} for (a, b), n in repairs.items()], mapping, alignment_gaps


def main():
    results = []
    for example in NAMES:
        path = folder(example)
        buf = (path / 'distill-compact.cpp').read_bytes()
        raw, report = reports(path / 'distill-compact.rep')
        decls, errors = declarations(buf)
        assert not errors, (example, errors)
        dg, rg = collections.defaultdict(list), collections.defaultdict(list)
        for d in decls:
            dg[d['name']].append(d)
        for r in report:
            if r['kept']:
                rg[r['name']].append(r)
        for name, ds in dg.items():
            rs = sorted(rg[name], key=lambda r: r['order'])
            if len(ds) == len(rs):
                for d, r in zip(sorted(ds, key=lambda d: d['start']), rs):
                    d['report'] = r
        ds = [d for d in decls if 'report' in d]
        owners = [-1] * len(buf)
        for i, d in sorted(enumerate(ds), key=lambda p: p[1]['end'] - p[1]['start'], reverse=True):
            owners[d['start']:d['end']] = [i] * (d['end'] - d['start'])
        # Directives own whole physical/logical lines ahead of declarations.
        pos, continued, directives = 0, False, collections.Counter()
        for line in buf.splitlines(keepends=True):
            isdir = continued or bool(re.match(rb'\s*#', line))
            if isdir:
                owners[pos:pos + len(line)] = [-2] * len(line)
                kind = 'generated guards' if b'DISTILL_ONCE_' in line else 'includes' if re.match(rb'#include', line) else 'other directives'
                directives[kind] += len(line)
            continued = isdir and bool(re.search(rb'\\[ \t]*\r?\n$', line))
            pos += len(line)
        counts = collections.Counter(owners)
        reasons = collections.Counter(directives)
        reasons['structural trivia/shells/unreported'] = counts[-1]
        for i, d in enumerate(ds):
            r = effective(raw, d['report'])
            d.update(bytes=counts[i], group=group(r['reason']), chain=cause_chain(raw, d['report']))
            reasons[d['group']] += d['bytes']
        headers, unmapped, repairs, line_map, alignment_gaps = provenance(example, buf, ds)
        assert sum(headers.values()) == sum(reasons.values()) == len(buf)
        # First-cause report graph only. This is deliberately not called a call
        # graph: AST edges also represent types and containment, and most edges
        # are omitted once a family already has its first reason.
        reached = {i for i, r in raw.items() if r['label'] == 'main'}
        while True:
            more = {i for i, r in raw.items() if r['kept'] and r['cause'] in reached and
                    (r['reason'].startswith('AST:') or r['reason'] == 'family aggregation')}
            if more <= reached:
                break
            reached |= more
        for d in ds:
            d['main_ast_first_cause'] = d['report']['id'] in reached
        verify_rep = (path / 'verify.rep').read_text()
        results.append(dict(example=example, compact=len(buf), projected=(len(buf)*8736 + 9999)//10000,
                            verify=(path/'verify.cpp').stat().st_size,
                            caide_fallback='optimizer output was not used' in verify_rep,
                            headers=headers, reasons=dict(reasons), declarations=ds,
                            unreported=[d for d in decls if 'report' not in d],
                            provenance_unmapped=unmapped, provenance_repairs=repairs,
                            alignment_gaps=alignment_gaps,
                            line_map=line_map, first_cause_reached=len(reached),
                            sha256={f: sha(path/f) for f in ['distill-compact.cpp','distill-compact.rep','verify.cpp','verify.rep']},
                            raw=list(raw.values())))
        print(example, len(buf), 'families', len(ds), 'unmapped', sum(x['bytes'] for x in unmapped), 'repairs', sum(x['bytes'] for x in repairs))
    dump('analysis.json', results)
    print('TOTAL', sum(x['compact'] for x in results))


if __name__ == '__main__':
    main()
