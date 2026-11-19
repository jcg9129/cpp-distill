# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail-closed, per-root textual libstdc++ feature-macro audit."""
from collections import deque
from functools import lru_cache
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT / 'ci/extensions'))
from generate import directive_text

VERSION = 2
NAME = r'[A-Za-z_][A-Za-z_0-9]*'
INTEGER = re.compile(r'(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\Z')
# Candidates, never unconditional exemptions: verify every reachable write.
PURE_CANDIDATES = frozenset({'_GLIBCXX_FLOAT_IS_IEEE_BINARY32',
                             '_GLIBCXX_DOUBLE_IS_IEEE_BINARY64'})


@lru_cache(maxsize=8192)
def identifiers(expression):
    # Header-name tokens are not identifiers. Keep the availability predicate
    # itself untrusted, even for a literal operand found on this machine.
    result, end = [], 0
    for match in re.finditer(r'\b(__has_include(?:_next)?)\s*\(', expression):
        if match.start() < end:
            continue
        result.append(expression[end:match.start()] + match[1])
        depth, end = 1, match.end()
        while end < len(expression) and depth:
            token = re.match(r'<[^>]*>|"(?:\\.|[^"\\])*"', expression[end:])
            if token:
                end += token.end()
                continue
            depth += (expression[end] == '(') - (expression[end] == ')')
            end += 1
        if depth:
            result.append(' <unparsed-condition> ')
    expression = ''.join(result) + expression[end:]
    expression = re.sub(r'\b(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\b', '0', expression)
    names = set(re.findall(NAME, expression)) - {'defined'}
    rest = re.sub(NAME, '', expression)
    if re.search(r'[^\s0()!~+*/%<>=&|^?:,\-]', rest):
        names.add('<unparsed-condition>')
    return frozenset(names)


def negative(name):
    return r'!\s*defined\s*(?:\(\s*' + re.escape(name) + r'\s*\)|' + re.escape(name) + r'\b)'


def negative_only(name, conditions):
    # Do not erase a negative atom nested in a negated expression (including
    # previous elif/else tests). We deliberately accept only simple own tests.
    return all(name not in identifiers(c) or
               not re.search(r'!\s*\(', c) and name not in identifiers(re.sub(negative(name), '1', c))
               for c in conditions)


def own_guard(name, conditions):
    def conjunct(expression):
        expression = expression.strip()
        if name not in identifiers(expression):
            return True
        # Strip only parentheses enclosing the entire expression.
        while expression.startswith('(') and expression.endswith(')'):
            depth, closes = 0, None
            for i, c in enumerate(expression):
                depth += (c == '(') - (c == ')')
                if depth == 0:
                    closes = i
                    break
            if closes != len(expression) - 1:
                break
            expression = expression[1:-1].strip()
        if re.fullmatch(negative(name), expression):
            return True
        depth, parts, start = 0, [], 0
        for i, c in enumerate(expression):
            depth += (c == '(') - (c == ')')
            if depth == 0 and expression[i:i+2] == '||':
                return False
            if depth == 0 and expression[i:i+2] == '&&':
                parts.append(expression[start:i])
                start = i + 2
        return bool(parts) and all(conjunct(p) for p in parts + [expression[start:]])
    return name.startswith(('__cpp_lib_', '__glibcxx_')) and all(conjunct(c) for c in conditions)



def directives(text):
    cleaned = directive_text(text)
    cleaned = re.sub(r'(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"',
                     '""', cleaned, flags=re.S)
    result = []
    for line in cleaned.splitlines():
        match = re.match(r'^\s*#\s*(' + NAME + r')\b(.*)', line)
        if match:
            result.append((match[1], match[2].strip()))
    return result


def wrapper_guards(instructions):
    """Match ifndef/define blocks with no alternative, including C wrappers.

    Keep the instruction positions: only writes at these positions can justify
    the guard, and only the corresponding negative test is exempted.
    """
    stack, candidates = [], {}
    for i, (op, body) in enumerate(instructions):
        if op in ('if', 'ifdef', 'ifndef'):
            candidate = (body if op == 'ifndef' and re.fullmatch(NAME, body)
                         and i + 1 < len(instructions)
                         and instructions[i + 1] in (('define', body), ('define', body + ' 1')) else None)
            stack.append([i, candidate])
        elif op in ('else', 'elif', 'elifdef', 'elifndef') and stack:
            stack[-1][1] = None
        elif op == 'endif' and stack:
            start, candidate = stack.pop()
            if candidate:
                candidates[start] = candidate
    return candidates


def parse(text, file):
    instructions = directives(text)
    wrappers = wrapper_guards(instructions)
    stack, guard_stack, seen_else, writes, includes = [], [], [], [], []
    for i, (op, body) in enumerate(instructions):
        if op in ('if', 'ifdef', 'ifndef'):
            expression = body if op == 'if' else ('!' if op == 'ifndef' else '') + 'defined(' + body + ')'
            stack.append([expression])
            guard_stack.append(wrappers.get(i))
            seen_else.append(False)
        elif op in ('elif', 'elifdef', 'elifndef', 'else'):
            if not stack or seen_else[-1]:
                raise ValueError(file + ': unmatched ' + op)
            if op == 'else':
                seen_else[-1] = True
            else:
                stack[-1].append(body if op == 'elif' else ('!' if op == 'elifndef' else '') + 'defined(' + body + ')')
        elif op == 'endif':
            if not stack:
                raise ValueError(file + ': unmatched endif')
            stack.pop()
            guard_stack.pop()
            seen_else.pop()
        elif op in ('define', 'undef', 'include', 'include_next'):
            conditions = [('!(' + c + ')') if otherwise or i < len(branch) - 1 else c
                          for branch, otherwise in zip(stack, seen_else) for i, c in enumerate(branch)]
            deps = set().union(*(identifiers(c) for c in conditions)) if conditions else set()
            if op in ('define', 'undef'):
                match = re.fullmatch('(' + NAME + r')(.*)', body)
                if not match or (op == 'undef' and match[2].strip()):
                    raise ValueError(file + ': malformed macro write')
                name, value = match.groups()
                writes.append(dict(name=name, value=value.strip() if op == 'define' else None,
                                   dependencies=sorted(deps - {name} if own_guard(name, conditions) else deps),
                                   conditions=conditions, file=file, guard_tests=[g for g in guard_stack if g],
                                   guard=wrappers.get(i - 1) if op == 'define' else None))
            else:
                includes.append((op, body, deps, conditions, [g for g in guard_stack if g]))
    if stack:
        raise ValueError(file + ': unterminated conditional')
    return writes, includes, set(wrappers.values())


def propagate(evidence, successors):
    """Union evidence to a fixed point, including every path and cycle."""
    pending = deque(name for name, values in evidence.items() if values)
    queued = set(pending)
    while pending:
        source = pending.popleft()
        queued.remove(source)
        for target in successors[source]:
            before = len(evidence[target])
            evidence[target].update(evidence[source])
            if len(evidence[target]) != before and target not in queued:
                pending.append(target)
                queued.add(target)


def dependency_expander(edges, seeds):
    """Expand once per SCC; cyclic request/value plumbing fails closed."""
    indices, low, stack, active, results = {}, {}, [], set(), {}
    def connect(name):
        indices[name] = low[name] = len(indices)
        stack.append(name)
        active.add(name)
        for dep in sorted(edges.get(name, ())):
            if dep not in indices:
                connect(dep)
                low[name] = min(low[name], low[dep])
            elif dep in active:
                low[name] = min(low[name], indices[dep])
        if low[name] != indices[name]:
            return
        members = set()
        while True:
            member = stack.pop()
            active.remove(member)
            members.add(member)
            if member == name:
                break
        common = set()
        for member in members:
            common.update(seeds.get(member, {member}))
            for dep in edges.get(member, ()):
                if dep not in members:
                    common.update(results[dep])
        if len(members) > 1 or name in edges.get(name, ()):
            common.update(members)
        for member in members:
            results[member] = common.copy()
    for name in sorted(edges):
        if name not in indices:
            connect(name)
    return lambda name: results.get(name, frozenset({name}))


def classify(files, predefines, blocked=frozenset(), *, roots=None):
    """Audit fresh TUs rooted at the requested search-relative header spellings.

    Literal edges follow compiler search order, including quote and include_next
    rules. All lexical branches participate. Unresolved literals are unavailable
    in the recorded closure; computed operands and malformed reachable files
    taint the entire root, including absence claims. Other roots cannot taint it.
    """
    parsed, unparsed = {}, {}
    for file, text in sorted(files.items()):
        try:
            parsed[file] = parse(text, file)
        except ValueError as error:
            unparsed[file] = str(error)
            parsed[file] = ([], [(op, body, {'<unparsed-file:' + file + '>'}, None, [])
                                for op, body in directives(text)
                                if op in ('include', 'include_next')], set())
    writes = {}
    for ws, _, _ in parsed.values():
        for w in ws:
            writes.setdefault(w['name'], []).append(w)
    for file in unparsed:
        for name in re.findall(r'\b__cpp_lib_[A-Za-z_0-9]+\b', files[file]):
            writes.setdefault(name, [])
    candidates = set().union(*(g for _, _, g in parsed.values()))
    guards = {g for g in candidates if g not in predefines and g not in blocked
              and all(w['guard'] == g and w['value'] in ('', '1') for w in writes[g])
              and len({files[w['file']] for w in writes[g]}) == 1}
    by_name = {}
    for file in sorted(files, key=lambda f: (int(f.split('/', 1)[0]), f)):
        by_name.setdefault(file.split('/', 1)[1], []).append(file)

    def resolve(name, source=None, kind='include', quoted=False):
        if quoted and source and kind == 'include':
            local = str(Path(source).parent / name).replace('\\', '/')
            if local in files:
                return local
        name = str(Path(name)).replace('\\', '/')
        begin = int(source.split('/', 1)[0]) + 1 if source and kind == 'include_next' else 0
        return next((f for f in by_name.get(name, ()) if int(f.split('/', 1)[0]) >= begin), None)

    def clean(deps, conditions, guard_tests):
        # Exempt only the actual wrapper test, never arbitrary uses of its
        # name. The matching define precedes all includes in this block.
        remaining = list(conditions or [])
        removable = set()
        for guard in guard_tests:
            if guard in guards:
                remaining.remove('!defined(' + guard + ')')
                removable.add(guard)
        return set(deps) - {g for g in removable if not any(g in identifiers(c) for c in remaining)}

    guard_owners = {g: {w['file'] for w in writes[g]} for g in guards}
    include_evidence, edges, taints = [], {f: [] for f in files}, {f: set() for f in files}
    for file, (_, includes, _) in parsed.items():
        if file in unparsed:
            taints[file].add('<unparsed-file:' + file + '>')
        for kind, operand, deps, conditions, guard_tests in includes:
            match = re.fullmatch(r'(?:<([^>\n]+)>|"([^"\n]+)")', operand)
            target = resolve(match[1] or match[2], file, kind, bool(match[2])) if match else None
            include_evidence.append(dict(file=file, kind=kind, operand=operand,
                                         dependencies=sorted(deps), conditions=conditions, target=target))
            if not match:
                taints[file].update(deps | {'<computed-include>'})
            elif (match[1] or match[2]).startswith('/') or re.match(r'^[A-Za-z]:', match[1] or match[2]):
                taints[file].add('<absolute-include>')
            elif target:
                edges[file].append((target, clean(deps, conditions, guard_tests), conditions))
    compiler_inputs = ((set(predefines) | {'__cplusplus', '__STRICT_ANSI__'}) - set(blocked)
                       - {'__has_include', '__has_include_next'})
    # Undefined request alternatives are known false in a fresh TU only when
    # actual installed writes establish their role (a prefix is insufficient).
    request_roles = {n for n, ws in writes.items() if n.startswith('__glibcxx_want_')
                     and any(w['value'] in ('', None) for w in ws)} - set(predefines) - set(blocked)

    def plumbing(name):
        return (name in requests or name.startswith('__glibcxx_') and
                '__cpp_lib_' + name[10:] in writes)

    results = {}
    for root in sorted(by_name if roots is None else roots):
        start = resolve(root)
        if start is None:
            results[root] = dict(macros={}, taint=['<missing-root>'])
            continue
        reached, pending = set(), [start]
        while pending:
            file = pending.pop()
            if file not in reached:
                reached.add(file)
                pending.extend(t for t, _, _ in edges[file])
        inbound = {f: set() for f in reached}
        successors = {f: set() for f in reached}
        unknown = set().union(*(taints[f] for f in reached))
        cut_cache = {}
        def external_guards(deps, conditions, cut=None):
            result = set(deps)
            for guard in guards & result:
                if not conditions or not negative_only(guard, conditions):
                    continue
                if cut is None:
                    before = reached
                else:
                    # If no guard writer can be reached without this edge,
                    # its negative guard is true on first traversal in a fresh
                    # TU. A different earlier/sibling route defeats the proof.
                    if cut not in cut_cache:
                        before, pending = set(), [start]
                        while pending:
                            source = pending.pop()
                            if source not in before:
                                before.add(source)
                                pending.extend(t for t, _, _ in edges[source] if (source, t) != cut)
                        cut_cache[cut] = before
                    before = cut_cache[cut]
                if not (guard_owners[guard] & before):
                    result.remove(guard)
            return result

        for file in reached:
            for target, deps, conditions in edges[file]:
                inbound[target].update(external_guards(deps, conditions, (file, target)))
                successors[file].add(target)
        propagate(inbound, successors)
        local = {n: [w for w in ws if w['file'] in reached] for n, ws in writes.items()}
        local = {n: ws for n, ws in local.items() if ws}
        requests = {n for n in request_roles
                    if all(w['value'] in ('', None) for w in local.get(n, []))}
        compiler = compiler_inputs - local.keys()
        dependency_edges = {n: set() for n in compiler | (requests - local.keys())}
        seeds = {n: set() for n in dependency_edges}
        value_sets = {n: set() for n in local}
        value_users = {n: set() for n in local}

        def direct(w):
            deps = external_guards(clean(w['dependencies'], w['conditions'], w['guard_tests']),
                                   w['conditions'])
            # Internal values share their corresponding feature's own-ifndef.
            if w['name'].startswith('__glibcxx_'):
                feature = '__cpp_lib_' + w['name'][10:]
                if own_guard(feature, w['conditions']):
                    deps.discard(feature)
            # Own-ifndef plumbing never erases an include-path condition.
            return deps | inbound[w['file']]

        for name, ws in local.items():
            deps, bad = set(), set()
            for w in ws:
                body = w['value']
                if body is not None and INTEGER.fullmatch(body):
                    value_sets[name].add(body)
                elif body in local and plumbing(body):
                    value_users[body].add(name)
                if not (body is not None and INTEGER.fullmatch(body)):
                    if body and plumbing(body):
                        deps.add(body)
                    elif not (name in requests and body in ('', None)):
                        bad.add(name)
                deps.update(direct(w))
            if name in PURE_CANDIDATES or plumbing(name):
                dependency_edges[name] = deps | unknown
                seeds[name] = bad
        propagate(value_sets, value_users)
        expand = dependency_expander(dependency_edges, seeds)
        macros = {}
        for name, ws in sorted(local.items()):
            if not name.startswith('__cpp_lib_'):
                continue
            pairs, all_deps, all_bad = {}, set(unknown), set(unknown)
            for w in ws:
                deps = direct(w) | unknown
                body = w['value']
                vals = ({body} if body is not None and INTEGER.fullmatch(body)
                        else value_sets.get(body, set()) if plumbing(body or '') else set())
                if body and plumbing(body):
                    deps.add(body)
                bad = set().union(*(expand(d) for d in deps)) if deps else set()
                if not vals:
                    bad.add('<nonliteral-or-undef>')
                all_deps.update(deps)
                all_bad.update(bad)
                for value in vals:
                    entry = pairs.setdefault(value, [set(), set()])
                    entry[0].update(deps)
                    entry[1].update(bad)
            # Undef/unsupported writes can invalidate every literal pair.
            invalid = any(w['value'] is None or not (INTEGER.fullmatch(w['value']) or
                          plumbing(w['value']) and value_sets.get(w['value'])) for w in ws)
            macros[name] = dict(values=sorted(value_sets[name]), independent=not all_bad,
                                dependencies=sorted(all_deps), unrecognised=sorted(all_bad),
                                pairs={v: dict(independent=not (bad or invalid),
                                              dependencies=sorted(deps),
                                              unrecognised=sorted(bad | ({'<nonliteral-or-undef>'} if invalid else set())))
                                       for v, (deps, bad) in sorted(pairs.items())})
        results[root] = dict(macros=macros, taint=sorted(unknown))
    return dict(generator=VERSION, pure_candidates=sorted(PURE_CANDIDATES), roots=results,
                # Definitions and conditions live once in the full artifact.
                macros={n: ws for n, ws in sorted(writes.items()) if n.startswith('__cpp_lib_')},
                support={n: ws for n, ws in sorted(writes.items()) if n in PURE_CANDIDATES or n.startswith('__glibcxx_')},
                guards=sorted(guards), includes=include_evidence, unparsed=unparsed,
                compiler_inputs={n: predefines[n] if isinstance(predefines, dict) else None
                                 for n in sorted(set(predefines) & compiler_inputs)},
                blocked_inputs=sorted(blocked))
