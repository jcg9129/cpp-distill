#!/usr/bin/env python3
"""Counterfactual spelling measurements. All writes stay under --work and --out."""
import argparse
from collections import Counter
from dataclasses import dataclass
from functools import lru_cache
import hashlib
import itertools
import json
from pathlib import Path
import re
import string
import time

from clang_inventory import inventory, raw_tokens

IDENT = re.compile(r'[A-Za-z_][A-Za-z_0-9]*')
KEYWORDS = set('''alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept
auto bitand bitor bool break case catch char char8_t char16_t char32_t class compl concept
const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype
default delete do double dynamic_cast else enum explicit export extern false float for friend
goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq
private protected public reflexpr register reinterpret_cast requires return short signed sizeof
static static_assert static_cast struct switch synchronized template this thread_local throw
true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor
xor_eq final override import module transaction_safe transaction_safe_dynamic'''.split())
PUNCT = sorted((' %:%: <=> >>= <<= ->* ... ## :: .* -> ++ -- << >> <= >= == != && || '
                '*= /= %= += -= &= ^= |= <: :> <% %> %:').split(), key=len, reverse=True)
LITERAL_START = re.compile(r'(?:u8|u|U|L)?(?:R"|["\'])')
SPLICE = re.compile(r'\\[ \t\v\f]*\r?\n')


@dataclass(frozen=True)
class Token:
    kind: str
    text: str
    start: int
    end: int
    directive: int


def lex(source):
    """ASCII C++ preprocessing tokens, offsets into original latin1 byte string.

    Splices are removed before lexing; raw literals recover their original bytes.
    Header names, comments, literals and pp-numbers are indivisible.
    """
    positions, chunks, last = [], [], 0
    for m in SPLICE.finditer(source):
        chunks.append(source[last:m.start()])
        positions.extend(range(last, m.start()))
        last = m.end()
    chunks.append(source[last:])
    positions.extend(range(last, len(source)))
    s = ''.join(chunks)
    result, i, bol, directive, serial = [], 0, True, 0, 0
    line_tokens = []
    while i < len(s):
        if s[i].isspace():
            if s[i] == '\n':
                bol, directive, line_tokens = True, 0, []
            i += 1
            continue
        start = i
        kind = 'punct'
        if s.startswith('//', i):
            kind = 'line-comment'
            j = s.find('\n', i + 2)
            i = len(s) if j < 0 else j
        elif s.startswith('/*', i):
            kind = 'comment'
            j = s.find('*/', i + 2)
            i = len(s) if j < 0 else j + 2
        elif ((line_tokens == ['#', 'include'] or
               len(line_tokens) >= 2 and line_tokens[-2:] in
               [['__has_include', '('], ['__has_include_next', '(']]) and s[i] in '<"'):
            kind = 'header'
            j = s.find('>' if s[i] == '<' else '"', i + 1)
            if j < 0:
                raise ValueError('Unterminated header')
            i = j + 1
        elif m := LITERAL_START.match(s, i):
            kind = 'literal'
            if 'R"' in m.group():
                j = s.find('(', m.end())
                delimiter = s[m.end():j]
                if j < 0 or len(delimiter) > 16:
                    raise ValueError('Invalid raw delimiter')
                close = ')' + delimiter + '"'
                raw_end = source.find(close, positions[j] + 1)
                if raw_end < 0:
                    raise ValueError('Unterminated raw string')
                raw_end += len(close)
                import bisect
                i = bisect.bisect_left(positions, raw_end)
            else:
                quote = m.group()[-1]
                i = m.end()
                while i < len(s):
                    if s[i] == quote:
                        i += 1
                        break
                    i += 2 if s[i] == '\\' else 1
            # User-defined suffix is part of the literal token and stays intact.
            if suffix := IDENT.match(s, i):
                i = suffix.end()
        elif m := IDENT.match(s, i):
            kind, i = 'id', m.end()
        elif s[i].isdigit() or s[i] == '.' and i + 1 < len(s) and s[i + 1].isdigit():
            kind, i = 'number', i + 1
            while i < len(s):
                c = s[i]
                if c.isalnum() or c in '_.':
                    i += 1
                elif c == "'" and i + 1 < len(s) and (s[i + 1].isalnum() or s[i + 1] == '_'):
                    i += 2
                elif c in '+-' and s[i - 1] in 'eEpP':
                    i += 1
                else:
                    break
        else:
            p = next((p for p in PUNCT if s.startswith(p, i)), s[i])
            if s.startswith('<::', i) and (i + 3 == len(s) or s[i + 3] not in ':>'):
                p = '<'
            i += len(p)
        text = s[start:i]
        if kind == 'literal':
            text = source[positions[start]:positions[i - 1] + 1]
        if bol and text in ('#', '%:'):
            serial += 1
            directive = serial
        result.append(Token(kind, text, positions[start], positions[i - 1] + 1, directive))
        if kind not in ('comment', 'line-comment'):
            bol = False
            line_tokens.append(text)
    return result


@lru_cache(maxsize=None)
def need_space(left_kind, left, right_kind, right):
    if left_kind == 'line-comment':
        return False
    if left_kind == 'header' or right_kind == 'header':
        # Header-name lexing depends on the surrounding #include / has_include.
        return False
    # Relexing also handles comment formation, UDL/prefix merging and digraphs.
    try:
        toks = lex(left + right)
    except ValueError:
        return True
    return [(t.kind, t.text) for t in toks] != [(left_kind, left), (right_kind, right)]


def macro_exclusions(tokens, source):
    groups = {}
    for t in tokens:
        if t.directive:
            groups.setdefault(t.directive, []).append(t)
    kept, force_space, force_join = set(), set(), set()
    for ts in groups.values():
        if len(ts) < 3 or ts[1].text != 'define':
            continue
        name = ts[2]
        kept.add(name.text)
        body = 3
        function = len(ts) > 3 and ts[3].text == '(' and not SPLICE.sub('', source[name.end:ts[3].start])
        if function:
            force_join.add((name.start, ts[3].start))
            body = 4
            while body < len(ts) and ts[body].text != ')':
                if ts[body].kind == 'id':
                    kept.add(ts[body].text)
                body += 1
            body += 1
        elif len(ts) > 3 and ts[3].text == '(':
            force_space.add((name.start, ts[3].start))
        if any(t.text in ('#', '##', '%:', '%:%:') for t in ts[body:]):
            # Conservative: every identifier in such a replacement list.
            kept.update(t.text for t in ts[body:] if t.kind == 'id')
    return kept, force_space, force_join


def exclusions(tokens, source, library, adl):
    kept, force_space, force_join = macro_exclusions(tokens, source)
    kept |= library | KEYWORDS | adl | {'main'}
    attr_depth, gnu_depth, pending_gnu = 0, 0, False
    for n, t in enumerate(tokens):
        if t.kind == 'literal':
            prefix = LITERAL_START.match(t.text)
            if 'R"' in prefix.group():
                opening = t.text.index('(', prefix.end())
                delimiter = t.text[prefix.end():opening]
                closing = t.text.rindex(')'+delimiter+'"')
                contents = t.text[opening+1:closing]
                suffix = t.text[closing+len(delimiter)+2:]
                string_literal = True
            else:
                quote = prefix.group()[-1]
                closing = prefix.end()
                while closing < len(t.text):
                    if t.text[closing] == quote:
                        break
                    closing += 2 if t.text[closing] == '\\' else 1
                contents = t.text[prefix.end():closing]
                suffix = t.text[closing+1:]
                string_literal = quote == '"'
            if suffix:
                kept.add(suffix)
            if string_literal:
                kept.update(IDENT.findall(contents))
                if 'R"' not in prefix.group():
                    # Also catch names encoded with octal/hex escapes.
                    decoded = re.sub(r'\\(?:x([0-9A-Fa-f]+)|([0-7]{1,3}))',
                                     lambda m: chr(int(m[1],16) if m[1] else int(m[2],8)), contents)
                    kept.update(IDENT.findall(decoded))
        if t.text == '[' and n + 1 < len(tokens) and tokens[n + 1].text == '[':
            attr_depth += 1
        if attr_depth and t.kind == 'id':
            kept.add(t.text)
        if t.text == ']' and n and tokens[n - 1].text == ']' and attr_depth:
            attr_depth -= 1
        if t.text in ('__attribute__', '__declspec'):
            pending_gnu = True
        elif pending_gnu and t.text == '(':
            gnu_depth, pending_gnu = gnu_depth + 1, False
        elif gnu_depth:
            if t.text == '(':
                gnu_depth += 1
            elif t.text == ')':
                gnu_depth -= 1
            elif t.kind == 'id':
                kept.add(t.text)
        if t.kind == 'id' and ('__' in t.text or re.match(r'_[A-Z]', t.text)):
            kept.add(t.text)
    return kept, force_space, force_join


def candidate_names():
    first = string.ascii_lowercase + string.ascii_uppercase
    rest = first + string.digits + '_'
    for length in itertools.count(1):
        for head in first:
            for tail in itertools.product(rest, repeat=length - 1):
                name = head + ''.join(tail)
                if '__' not in name:
                    yield name


def mapping_for(tokens, declared, kept):
    counts = Counter(t.text for t in tokens if t.kind == 'id')
    eligible = {n for n in declared if IDENT.fullmatch(n)} & counts.keys() - kept
    # Simultaneous substitution permits reusing another renamed old spelling.
    # Every identifier that is not eligible is a kept spelling, even if unresolved.
    blocked = (set(counts) - eligible) | kept
    names = (n for n in candidate_names() if n not in blocked)
    mapping = {}
    for old in sorted(eligible, key=lambda n: (-counts[n], n)):
        new = next(names)
        mapping[old] = new
    return mapping, counts


def render(source, tokens, mapping=None, minimal=False):
    mapping = mapping or {}
    if not minimal:
        parts, end = [], 0
        for t in tokens:
            if t.kind == 'id' and t.text in mapping:
                parts.extend((source[end:t.start], mapping[t.text]))
                end = t.end
        parts.append(source[end:])
        return ''.join(parts)
    _, force_space, force_join = macro_exclusions(tokens, source)
    parts, previous, previous_text, tail = [], None, '', []
    for t in tokens:
        text = mapping.get(t.text, t.text) if t.kind == 'id' else t.text
        original = source[t.start:t.end]
        display = original
        if t.kind == 'id' and t.text in mapping:
            display = ''.join(SPLICE.findall(original)) + text
        sep = ''
        if previous:
            if previous.directive != t.directive and (previous.directive or t.directive):
                sep = '\n'
            elif previous.kind == 'line-comment':
                sep = '\n'
            elif (previous.start, t.start) in force_space:
                sep = ' '
            elif (previous.start, t.start) not in force_join:
                if need_space(previous.kind, previous_text, t.kind, text):
                    sep = ' '
                # < :: > and . . . need three-token context. Header names and
                # comments require their original lexical context instead.
                if not sep and tail and all(k not in ('header','comment','line-comment') for k, _ in tail) and t.kind != 'header':
                    expected = tail + [(t.kind,text)]
                    try:
                        actual = [(u.kind,u.text) for u in lex(''.join(v for _,v in expected))]
                    except ValueError:
                        actual = []
                    if actual != expected:
                        sep = ' '
        # Continuations are source spelling, not removable inter-token spaces.
        # Keep their backslashes and required newlines, as the trim review does.
        gap = source[previous.end:t.start] if previous else source[:t.start]
        splices = ''.join('\\\n' for _ in SPLICE.finditer(gap))
        sep = splices + sep if sep == '\n' else sep + splices
        parts.extend((sep, display))
        tail = ([(t.kind,text)] if sep else (tail+[(t.kind,text)])[-3:])
        previous, previous_text = t, text
    if previous and (previous.directive or previous.kind == 'line-comment'):
        parts.append('\n')
    return ''.join(parts)


def cleanup(source, tokens):
    counts = Counter(t.text for t in tokens if t.kind == 'id')
    seen, include_bytes, define_bytes, ranges = set(), 0, 0, []
    groups = {}
    for t in tokens:
        if t.directive:
            groups.setdefault(t.directive, []).append(t)
    for ts in groups.values():
        a = source.rfind('\n', 0, ts[0].start) + 1
        b = source.find('\n', ts[-1].end)
        b = len(source) if b < 0 else b + 1
        if len(ts) == 3 and ts[1].text == 'include' and ts[2].kind == 'header' and ts[2].text.startswith('<'):
            header = ts[2].text
            if header in seen:
                include_bytes += b - a
                ranges.append((a, b))
            seen.add(header)
        if (len(ts) == 3 and ts[1].text == 'define' and ts[2].text.startswith('DISTILL_ONCE_')
                and counts[ts[2].text] == 1):
            define_bytes += b - a
            ranges.append((a, b))
    end, parts = 0, []
    for a, b in sorted(ranges):
        parts.append(source[end:a])
        end = b
    parts.append(source[end:])
    return ''.join(parts), include_bytes, define_bytes


def signature(tokens):
    return [(t.kind, t.text, t.directive) for t in tokens]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=Path('/home/appuser/.cache/caide-gap/caide2'))
    parser.add_argument('--work', type=Path, default=Path('measurements/minify-analysis'))
    parser.add_argument('--out', type=Path, default=Path('docs/reviews/minify-analysis'))
    parser.add_argument('--only', default='')
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    args.out.mkdir(parents=True, exist_ok=True)
    gcc = (args.work / 'gcc13/usr/include').resolve()
    flags = ['-x', 'c++', '-std=c++23', '-nostdinc++', '-isystem', str(gcc / 'c++/13'),
             '-isystem', str(gcc / 'aarch64-linux-gnu/c++/13'),
             '-isystem', str(gcc / 'c++/13/backward'), '-resource-dir', '/usr/lib/llvm-22/lib/clang/22']
    adl = {l.split()[1] for l in Path('data/names/adl.txt').read_text().splitlines() if l and not l.startswith('#')}
    header_cache_path = args.work / 'header-identifiers.json'
    header_data = json.loads(header_cache_path.read_text()) if header_cache_path.exists() else {}
    header_cache = header_data.get('files',{}) if header_data.get('version')==2 else {}
    rows, audits = [], {}
    sources = sorted(args.input.glob('artifacts/caide-compare-shard-*/examples/*/distill-compact.cpp'), key=lambda p:p.parent.name)
    summary = {a[0]:int(a[6]) for l in (args.input / 'summary.tsv').read_text().splitlines()[1:]
               if (a := l.split('\t'))[1] == 'distill-compact'}
    for number, path in enumerate(sources, 1):
        name = path.parent.name
        if args.only and name != args.only:
            continue
        start = time.monotonic()
        source = path.read_bytes().decode('latin1')
        assert len(source) == summary[name], (name, len(source), summary[name])
        toks = lex(source)
        cache = args.work / (name + '.inventory.json')
        key = hashlib.sha256((source + '\n' + '\n'.join(flags)).encode('latin1')).hexdigest()
        if cache.exists() and (inv := json.loads(cache.read_text())).get('key') == key:
            pass
        else:
            inv = inventory(path.resolve(), flags)
            inv['key'] = key
            cache.write_text(json.dumps(inv, indent=2) + '\n')
        library = set()
        for header in inv['headers']:
            digest = hashlib.sha256(Path(header).read_bytes()).hexdigest()
            if header not in header_cache or header_cache[header]['sha256'] != digest:
                # Use Clang's raw identifier tokens, as in the A12 inventory;
                # this also conservatively counts components of #include names.
                header_cache[header] = dict(sha256=digest,
                    names=sorted({s for k,s in raw_tokens(Path(header)) if k==2}))
            library.update(header_cache[header]['names'])
        header_cache_path.write_text(json.dumps(dict(version=2,files=header_cache),sort_keys=True)+'\n')
        kept, _, _ = exclusions(toks, source, library, adl)
        mapping, counts = mapping_for(toks, inv['declared'], kept)
        t1 = render(source, toks, minimal=True)
        t2 = render(source, toks, mapping)
        both = render(source, toks, mapping, minimal=True)
        cleaned, inc_bytes, def_bytes = cleanup(source, toks)
        all_rules = render(cleaned, lex(cleaned), mapping, minimal=True)
        assert len(source)-len(t2) == sum(counts[k]*(len(k)-len(v)) for k,v in mapping.items())
        # Exact preprocessing-token and directive-group equivalence for T1.
        assert signature(lex(t1)) == signature(toks), name + ': T1 tokens differ'
        expected = [(t.kind, mapping.get(t.text,t.text) if t.kind == 'id' else t.text, t.directive) for t in toks]
        assert signature(lex(t2)) == expected, name + ': T2 tokens differ'
        assert signature(lex(both)) == expected, name + ': combined tokens differ'
        assert all(n not in kept and n not in (set(counts)-mapping.keys()) and n not in KEYWORDS for n in mapping.values())
        row = dict(example=name, compact=len(source), t1=len(t1), t2=len(t2), both=len(both),
                   duplicate_include_bytes=inc_bytes, unused_once_bytes=def_bytes, all_rules=len(all_rules),
                   renamed_spellings=len(mapping), library_identifiers=len(library), headers=len(inv['headers']))
        rows.append(row)
        audits[name] = dict(source_sha256=hashlib.sha256(source.encode('latin1')).hexdigest(),
                           report_sha256=hashlib.sha256(path.with_suffix('.rep').read_bytes()).hexdigest(),
                           renames={k:dict(new=v,occurrences=counts[k],saved=counts[k]*(len(k)-len(v))) for k,v in mapping.items()},
                           declared_spellings=len(inv['declared']), library_identifiers=len(library),
                           header_sha256={h:hashlib.sha256(Path(h).read_bytes()).hexdigest() for h in inv['headers']})
        dest = args.work / 'outputs' / name
        dest.mkdir(parents=True, exist_ok=True)
        for variant, text in [('t1',t1), ('t2',t2), ('both',both), ('all_rules',all_rules)]:
            (dest / (variant+'.cpp')).write_bytes(text.encode('latin1'))
        print(f'{number}/{len(sources)} {name}: {len(source)} -> {len(both)} ({time.monotonic()-start:.2f}s)', flush=True)
        (args.work / 'partial.json').write_text(json.dumps(rows,indent=2)+'\n')
    columns = ['compact','t1','t2','both','all_rules']
    result = dict(examples=len(rows),totals={c:sum(r[c] for r in rows) for c in columns},
                  over={str(limit):{c:sum(r[c]>limit for r in rows) for c in columns} for limit in [65536,50000,32768]},
                  still_over_64k=[{c:r[c] for c in ['example','both','all_rules']} for r in rows if r['both']>65536],
                  duplicate_include_bytes=sum(r['duplicate_include_bytes'] for r in rows),
                  unused_once_bytes=sum(r['unused_once_bytes'] for r in rows),rows=rows)
    suffix = '.'+args.only if args.only else ''
    (args.out / ('results'+suffix+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    (args.work / ('renames'+suffix+'.json')).write_text(json.dumps(audits,indent=2)+'\n')
    fields = list(rows[0])
    (args.out / ('results'+suffix+'.tsv')).write_text('\t'.join(fields)+'\n'+''.join('\t'.join(str(r[f]) for f in fields)+'\n' for r in rows))
    print(json.dumps({k:v for k,v in result.items() if k!='rows'},indent=2))


if __name__ == '__main__':
    main()
