# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail-closed textual libstdc++ feature-macro audit, including inactive branches."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT / 'ci/extensions'))
from generate import directive_text

VERSION = 1
NAME = r'[A-Za-z_][A-Za-z_0-9]*'
INTEGER = re.compile(r'(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\Z')
# Reviewed candidates, NOT unconditional exemptions. Each installed definition,
# undefinition, replacement and controlling condition is verified below. No
# _GLIBCXX_USE_*, _GLIBCXX_HAVE_*, ABI/hosted/thread/locale switch is admitted.
PURE_CANDIDATES = frozenset({'_GLIBCXX_FLOAT_IS_IEEE_BINARY32',
                             '_GLIBCXX_DOUBLE_IS_IEEE_BINARY64'})


def identifiers(expression):
    # Remove pp-numbers before identifiers, so 201703L does not depend on L.
    expression = re.sub(r'\b(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\b', '0', expression)
    names = set(re.findall(NAME, expression)) - {'defined'}
    rest = re.sub(NAME, '', expression)
    if re.search(r'[^\s0()!~+*/%<>=&|^?:,\-]', rest):
        names.add('<unparsed-condition>')
    return names


def own_guard(name, conditions):
    if not name.startswith(('__cpp_lib_', '__glibcxx_')):
        return False
    # Also accept an own-ifndef atom combined with request conditions in GCC's
    # generated #if. A value test or positive defined(self) remains unrecognised.
    negative = r'!\s*defined\s*(?:\(\s*' + re.escape(name) + r'\s*\)|' + re.escape(name) + r'\b)'
    return all(name not in identifiers(re.sub(negative, '1', c)) for c in conditions)


def parse(text, file):
    """Keep previous elif tests for else/elif; no expression evaluation."""
    cleaned = directive_text(text)
    # A raw literal can contain newlines that look like directives. Retain
    # quoted include operands, but erase raw literals before line matching.
    cleaned = re.sub(r'(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"',
                     '""', cleaned, flags=re.S)
    lines = cleaned.splitlines()
    directives = []
    for line in lines:
        match = re.match(r'^\s*#\s*(' + NAME + r')\b(.*)', line)
        if match:
            directives.append((match[1], match[2].strip()))
    guard = None
    if len(directives) > 1 and directives[0][0] == 'ifndef':
        candidate = directives[0][1]
        if directives[1] == ('define', candidate) or directives[1] == ('define', candidate + ' 1'):
            # Only whole-file guards, never a configuration #ifndef.
            depth, end, alternative = 0, None, False
            for i, (op, _) in enumerate(directives):
                if op in ('if', 'ifdef', 'ifndef'):
                    depth += 1
                elif op in ('else', 'elif', 'elifdef', 'elifndef') and depth == 1:
                    alternative = True
                elif op == 'endif':
                    depth -= 1
                    if depth == 0:
                        end = i
                        break
            if end == len(directives) - 1 and not alternative:
                guard = candidate
    stack, writes, includes = [], [], []
    for op, body in directives:
        if op in ('if', 'ifdef', 'ifndef'):
            expression = body if op == 'if' else ('!' if op == 'ifndef' else '') + 'defined(' + body + ')'
            stack.append([expression])
        elif op in ('elif', 'elifdef', 'elifndef', 'else'):
            if not stack:
                raise ValueError(file + ': unmatched ' + op)
            if op != 'else':
                stack[-1].append(body if op == 'elif' else 'defined(' + body + ')')
        elif op == 'endif':
            if not stack:
                raise ValueError(file + ': unmatched endif')
            stack.pop()
        elif op in ('define', 'undef', 'include', 'include_next'):
            conditions = [c for branch in stack for c in branch]
            deps = set().union(*(identifiers(c) for c in conditions)) if conditions else set()
            if op in ('define', 'undef'):
                match = re.fullmatch('(' + NAME + r')(.*)', body)
                if not match:
                    raise ValueError(file + ': malformed macro write')
                name, value = match.groups()
                writes.append(dict(name=name, value=value.strip() if op == 'define' else None,
                                   dependencies=sorted(deps - {name} if own_guard(name, conditions) else deps),
                                   conditions=conditions, file=file))
            else:
                includes.append((op, body, deps, conditions))
    if stack:
        raise ValueError(file + ': unterminated conditional')
    return writes, includes, guard


def classify(files, predefines, blocked=frozenset()):
    """files maps stable search-directory-relative paths to installed contents.

    Include-edge conditions and request-macro writes participate transitively.
    Unknown computed includes conservatively taint every definition. This is
    intentionally stricter than parsing version.def or matching macro prefixes.
    """
    parsed = {f: parse(t, f) for f, t in sorted(files.items())}
    all_writes = [w for ws, _, _ in parsed.values() for w in ws]
    writes = {}
    for w in all_writes:
        writes.setdefault(w['name'], []).append(w)
    candidates = {g for _, _, g in parsed.values() if g}
    # A guard-looking use cannot exempt a build switch defined elsewhere.
    guards = {g for g in candidates if all(w['value'] in ('', '1') and
              parsed[w['file']][2] == g for w in writes.get(g, []))}
    inbound = {f: set() for f in files}
    include_evidence, edges, computed = [], [], set()
    by_name = {}
    for file in files:
        by_name.setdefault(file.split('/', 1)[-1], []).append(file)
    for file, (_, includes, _) in parsed.items():
        for kind, operand, deps, conditions in includes:
            include_evidence.append(dict(file=file, kind=kind, operand=operand, dependencies=sorted(deps), conditions=conditions))
            match = re.fullmatch(r'[<"]([^>"\n]+)[>"]', operand)
            if not match:
                computed.update(deps | {'<computed-include>'})
                continue
            local = str(Path(file).parent / match[1]).replace('\\', '/')
            targets = set(by_name.get(match[1], [])) | ({local} if local in files else set())
            for target in targets - {file}:
                inbound[target].update(deps - guards)
                edges.append((file, target))
    # Propagate ancestry to a fixed point; edges are indexed once, not found by
    # repeatedly comparing every include with every reachable header.
    changed = True
    while changed:
        changed = False
        for file, target in edges:
            before = len(inbound[target])
            inbound[target].update(inbound[file])
            changed |= len(inbound[target]) != before
    compiler = (set(predefines) | {'__cplusplus', '__STRICT_ANSI__'}) - writes.keys() - set(blocked)

    def plumbing(name):
        return (name.startswith('__glibcxx_want_') or name.startswith('__glibcxx_') and
                '__cpp_lib_' + name[10:] in writes)

    def values(name, seen=frozenset()):
        if name in seen:
            return set()
        result = set()
        for entry in writes.get(name, []):
            body = entry['value']
            if body is not None and INTEGER.fullmatch(body):
                result.add(body)
            elif body and plumbing(body):
                result.update(values(body, seen | {name}))
        return result

    def dependencies(name, seen=frozenset()):
        if name in compiler or name in guards:
            return set()
        if name in seen:
            return {name}  # cycles outside guards are unrecognised
        if name not in PURE_CANDIDATES and not plumbing(name):
            return {name}
        entries = writes.get(name, [])
        if not entries:
            return {name}
        bad = set()
        for w in entries:
            if w['value'] is not None and not (INTEGER.fullmatch(w['value']) or
                    (name.startswith('__glibcxx_want_') and w['value'] == '')):
                if plumbing(w['value']):
                    bad.update(dependencies(w['value'], seen | {name}))
                else:
                    bad.add(name)
            for dep in set(w['dependencies']) | inbound[w['file']] | computed:
                bad.update(dependencies(dep, seen | {name}))
        return bad

    result = {}
    for name, entries in sorted(writes.items()):
        if not name.startswith('__cpp_lib_'):
            continue
        bad, deps = set(), set()
        for w in entries:
            direct = set(w['dependencies']) | inbound[w['file']] | computed
            deps.update(direct)
            for dep in direct:
                bad.update(dependencies(dep))
            if w['value'] is None:
                bad.add('<nonliteral-or-undef>')
            elif not INTEGER.fullmatch(w['value']):
                if plumbing(w['value']) and values(w['value']):
                    deps.add(w['value'])
                    bad.update(dependencies(w['value']))
                else:
                    bad.add('<nonliteral-or-undef>')
        result[name] = dict(values=sorted(values(name)),
                            independent=not bad, dependencies=sorted(deps),
                            unrecognised=sorted(bad), definitions=entries)
    support = {n: entries for n, entries in sorted(writes.items()) if n in PURE_CANDIDATES or n.startswith('__glibcxx_')}
    return dict(generator=VERSION, pure_candidates=sorted(PURE_CANDIDATES), macros=result,
                support=support, includes=include_evidence,
                compiler_inputs={n: predefines[n] if isinstance(predefines, dict) else None
                                 for n in sorted(set(predefines) & compiler)},
                blocked_inputs=sorted(blocked))
