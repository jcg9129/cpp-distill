# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail-closed, per-root textual library macro audit."""
from collections import deque
from functools import lru_cache
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT / 'ci/extensions'))
from generate import directive_text

VERSION = 5
NAME = r'[A-Za-z_][A-Za-z_0-9]*'
INTEGER = re.compile(r"(?:0[xX][0-9a-fA-F](?:'?[0-9a-fA-F])*|0[bB][01](?:'?[01])*|"
                     r"0(?:'?[0-7])*|[1-9](?:'?[0-9])*)"
                     r'(?:[uU](?:ll|LL|[lLzZ])?|(?:ll|LL|[lLzZ])[uU]?)?\Z')
STANDARD_MACROS = frozenset(n for n in (ROOT / 'data/names/standard-macros.txt').read_text().splitlines()
                            if n and not n.startswith('#'))


def library_macro(name):
    return name.startswith('__cpp_lib_') or name in STANDARD_MACROS


# Candidates, never unconditional exemptions: verify every reachable write.
PURE_CANDIDATES = frozenset({'_GLIBCXX_FLOAT_IS_IEEE_BINARY32',
                             '_GLIBCXX_DOUBLE_IS_IEEE_BINARY64'})
# C++ mode supplies no C version macro; the IEC type request is a user opt-in.
# Their source-entry absence is usable only when actually absent, not overridden,
# and not written anywhere in this root. The default-library Boolean prevents
# user changes before using the corresponding header facts.
DEFAULT_ABSENT_INPUTS = frozenset({'__STDC_VERSION__', '__STDC_WANT_IEC_60559_TYPES_EXT__'})
HEADER_TEST = re.compile(r'\b(__has_include(?:_next)?)\s*\(\s*(<[^>\n]+>|"[^"\n]+")\s*\)')


def header_test_key(predicate, operand, file):
    # Quote lookup and include_next depend on the search-relative source file.
    return predicate + '(' + operand + ')@' + file.split('/', 1)[1]


@lru_cache(maxsize=8192)
def condition_inputs(expression, file):
    tests = set()
    def replace(match):
        tests.add(header_test_key(*match.groups(), file))
        return '0'
    rest = HEADER_TEST.sub(replace, expression)
    return frozenset(tests | set(identifiers(rest)))


def apply_trust(audit, inputs):
    """Project stored SCC requirements using explicitly justified build facts.

    Retaining the unfiltered requirements makes cross-build revocation possible
    without reparsing headers or rerunning the per-root graph audit at merge.
    """
    audit['trusted_inputs'] = inputs
    projections = {}
    for header, root in audit['roots'].items():
        used = set()
        for entry in root['macros'].values():
            used.update(entry['requirements'])
            for record in [entry, *entry['pairs'].values()]:
                key = tuple(record['requirements'])
                if key not in projections:
                    projections[key] = sorted(set(key) - inputs.keys())
                record['unrecognised'] = projections[key]
                record['independent'] = not projections[key]
        root['trusted_inputs'] = {n: rule for n, rule in inputs.items() if n in used}
        for name, requirements in root.get('condition_macros', {}).items():
            if not (set(requirements) - inputs.keys()):
                root['trusted_inputs'][name] = dict(rule='independent-library-macro', root=header)
    return audit


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
    return (library_macro(name) or name.startswith('__glibcxx_')) and all(conjunct(c) for c in conditions)



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
            deps = set().union(*(condition_inputs(c, file) for c in conditions)) if conditions else set()
            if op in ('define', 'undef'):
                match = re.fullmatch('(' + NAME + r')(.*)', body)
                if not match or (op == 'undef' and match[2].strip()):
                    raise ValueError(file + ': malformed macro write')
                name, value = match.groups()
                writes.append(dict(name=name, value=value.strip() if op == 'define' else None,
                                   function_like=op == 'define' and value.startswith('('),
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


def dependency_expander(edges, seeds, grounded=frozenset()):
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
        if (len(members) > 1 or name in edges.get(name, ())) and not members <= grounded:
            common.update(members)
        for member in members:
            results[member] = common.copy()
    for name in sorted(edges):
        if name not in indices:
            connect(name)
    return lambda name: results.get(name, frozenset({name}))


def grounded_macros(start, includes, intrinsic, writers, invalid):
    """Prove include-induced cycles from an entry path and lexical definitions.

    A macro needs all its lexical/value prerequisites and a reachable writer.
    An include needs a reachable source and all macro conditions. Each event
    fires once. Thus an unconditional definition can unlock a later conditional
    include returning to its provider, but an unanchored macro cycle cannot
    bootstrap itself. External inputs stay in the SCC requirements regardless.
    """
    subscribers, remaining, ready, proven = {}, {}, deque(), set()
    def clause(event, requirements):
        remaining[event] = len(requirements)
        for prerequisite in requirements:
            subscribers.setdefault(prerequisite, []).append(event)
        if not requirements:
            ready.append(event)
    names = intrinsic.keys()
    for name, deps in intrinsic.items():
        if name not in invalid:
            clause(('lexical', name), {('macro', n) for n in deps & names})
    for file, targets in includes.items():
        for i, (target, deps) in enumerate(targets):
            clause(('include', file, i, target), {('file', file)} | {('macro', n) for n in deps & names})
    ready.append(('file', start))
    emitted, lexical, available = set(), set(), set()
    def maybe(name):
        if name in lexical and name in available:
            ready.append(('macro', name))
    while ready:
        event = ready.popleft()
        if event in emitted:
            continue
        emitted.add(event)
        kind, name, *_ = event
        if kind == 'file':
            for macro in writers.get(name, ()):
                available.add(macro)
                maybe(macro)
        elif kind == 'lexical':
            lexical.add(name)
            maybe(name)
        elif kind == 'include':
            ready.append(('file', event[3]))
        elif kind == 'macro':
            proven.add(name)
        for dependent in subscribers.get(event, ()):
            remaining[dependent] -= 1
            if not remaining[dependent]:
                ready.append(dependent)
    return proven


def classify(files, predefines, blocked=frozenset(), *, roots=None, trusted_inputs=None):
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
    file_writes = {f: ws for f, (ws, _, _) in parsed.items()}
    for ws, _, _ in parsed.values():
        for w in ws:
            writes.setdefault(w['name'], []).append(w)
    for file in unparsed:
        for name in re.findall(NAME, files[file]):
            if library_macro(name):
                writes.setdefault(name, [])
    # Function-like writes are evidence against a previously object-like name,
    # but function-only standard names do not enter the captured universe.
    targets = {n for n, ws in writes.items() if library_macro(n) and
               (n.startswith('__cpp_lib_') or any(w['value'] is not None and not w['function_like'] for w in ws))}
    audited_names = targets | PURE_CANDIDATES
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
    entry_absences = DEFAULT_ABSENT_INPUTS - set(predefines) - set(blocked)
    compiler_inputs = ((set(predefines) | {'__cplusplus', '__STRICT_ANSI__'} | entry_absences) - set(blocked)
                       - {'__has_include', '__has_include_next'})
    # Undefined request alternatives are known false in a fresh TU only when
    # actual installed writes establish their role (a prefix is insufficient).
    request_roles = {n for n, ws in writes.items() if n.startswith('__glibcxx_want_')
                     and any(w['value'] in ('', None) for w in ws)} - set(predefines) - set(blocked)

    def plumbing(name):
        return (name in requests or name.startswith('__glibcxx_') and
                '__cpp_lib_' + name[10:] in writes)

    # These lexical proofs do not depend on the root. Avoid repeating them for
    # every write in every fresh TU (hundreds of thousands of calls on GCC 15).
    write_deps = {}
    for ws in file_writes.values():
        for w in ws:
            if w['name'] not in audited_names and not w['name'].startswith('__glibcxx_'):
                continue
            deps = clean(w['dependencies'], w['conditions'], w['guard_tests'])
            if w['name'].startswith('__glibcxx_'):
                feature = '__cpp_lib_' + w['name'][10:]
                if own_guard(feature, w['conditions']):
                    deps.discard(feature)
            write_deps[id(w)] = deps

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

        root_edges = {f: [] for f in reached}
        for file in reached:
            for target, deps, conditions in edges[file]:
                cleaned = external_guards(deps, conditions, (file, target))
                root_edges[file].append((target, cleaned))
                inbound[target].update(cleaned)
                successors[file].add(target)
        propagate(inbound, successors)
        local = {}
        for file in sorted(reached):
            for w in file_writes[file]:
                local.setdefault(w['name'], []).append(w)
        # GCC float.h resets FLT_EVAL_METHOD (and other standard names) before
        # defining it. A prefix of undefs in the sole writer file cannot erase
        # a subsequent positive definition observed by -dM. Retain all writes
        # in the evidence; ignore only this prefix for positive-value proofs.
        # Later undefs, or writers in another file, still invalidate all pairs.
        resets = []
        for name, ws in local.items():
            if name not in STANDARD_MACROS or len({w['file'] for w in ws}) != 1:
                continue
            first = next((i for i, w in enumerate(ws) if w['value'] is not None), len(ws))
            if first and first < len(ws) and all(w['value'] is not None for w in ws[first:]):
                local[name] = ws[first:]
                resets.append(name)
        requests = {n for n in request_roles
                    if all(w['value'] in ('', None) for w in local.get(n, []))}
        compiler = compiler_inputs - local.keys()
        audited_local = {n: ws for n, ws in local.items()
                         if n in audited_names or n.startswith('__glibcxx_')}
        # An alias to an actual compiler predefine uses this capture's argv,
        # never a worker/default compiler value. Local writes and volatile or
        # overridden inputs have already been removed from `compiler`.
        compiler_values = {}
        if isinstance(predefines, dict):
            definitions = {}
            for n in compiler & predefines.keys():
                match = re.fullmatch(r'#define ' + re.escape(n) + r' (.*)', predefines[n])
                if match:
                    definitions[n] = match[1]
            for n in definitions:
                body, seen = n, set()
                while body in definitions and body not in seen:
                    seen.add(body)
                    body = definitions[body]
                if INTEGER.fullmatch(body):
                    compiler_values[n] = body

        def value_alias(name):
            return plumbing(name) or name in compiler_values

        @lru_cache(maxsize=None)
        def replacement_dependencies(body):
            # Also retain unsupported/volatile value chains as dependencies.
            # Reproduced snapshots may use build-dependent integers, but must
            # never hide a native input behind an unrecognised value alias.
            pending, seen = [body], set()
            while pending:
                name = pending.pop()
                if not name or not re.fullmatch(NAME, name) or name in seen:
                    continue
                seen.add(name)
                for w in local.get(name, []):
                    pending.append(w['value'])
                    # An unsupported helper can select its value through a
                    # native condition or a conditional include as well as a
                    # value alias. Preserve those dependencies for own snapshots.
                    deps = write_deps.get(id(w))
                    if deps is None:
                        deps = clean(w['dependencies'], w['conditions'], w['guard_tests'])
                    pending.extend(external_guards(deps, w['conditions']) | inbound[w['file']])
                if isinstance(predefines, dict) and name in predefines:
                    match = re.fullmatch(r'#define ' + re.escape(name) + r' (.*)', predefines[name])
                    if match:
                        pending.append(match[1])
            return frozenset(seen)

        # Materialize only inputs actually used by this root, rather than all
        # hundreds of unused compiler predefines in every SCC graph.
        dependency_edges = {}
        seeds = {n: set() for n in dependency_edges}
        value_sets = {n: {v} for n, v in compiler_values.items()}
        value_sets.update({n: set() for n in audited_local})
        value_users = {n: set() for n in value_sets}

        def direct(w):
            deps = external_guards(write_deps[id(w)], w['conditions'])
            # Own-ifndef plumbing never erases an include-path condition.
            return deps | inbound[w['file']]

        direct_deps = {id(w): direct(w) for ws in audited_local.values() for w in ws}
        condition_names = set()
        intrinsic, writers, invalid_names = {}, {}, set()
        for name, ws in audited_local.items():
            deps, own, bad = set(), set(), set()
            for w in ws:
                writers.setdefault(w['file'], set()).add(name)
                own.update(external_guards(write_deps[id(w)], w['conditions']))
                body = w['value']
                aliases = replacement_dependencies(body)
                deps.update(aliases)
                own.update(aliases)
                if body is not None and INTEGER.fullmatch(body):
                    value_sets[name].add(body)
                elif body in value_sets and value_alias(body):
                    value_users[body].add(name)
                if not (body is not None and INTEGER.fullmatch(body)):
                    if body and value_alias(body):
                        deps.add(body)
                        own.add(body)
                    elif not (name in requests and body in ('', None)):
                        bad.add(name)
                deps.update(direct_deps[id(w)])
            condition_names.update(deps)
            dependency_edges[name] = deps | unknown
            seeds[name] = bad
            intrinsic[name] = own
            if bad:
                invalid_names.add(name)
        for name in condition_names & (compiler | (requests - local.keys())):
            dependency_edges[name] = set()
            seeds[name] = set()
        propagate(value_sets, value_users)
        grounded = grounded_macros(start, root_edges, intrinsic, writers, invalid_names)
        expand = dependency_expander(dependency_edges, seeds, grounded)
        macros = {}
        for name, ws in sorted(audited_local.items()):
            if name not in targets:
                continue
            pairs, all_deps, all_bad = {}, set(unknown), set(unknown)
            for w in ws:
                deps = direct_deps[id(w)] | unknown
                body = w['value']
                deps.update(replacement_dependencies(body))
                vals = ({body} if body is not None and INTEGER.fullmatch(body)
                        else value_sets.get(body, set()) if value_alias(body or '') else set())
                if body and value_alias(body):
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
                          value_alias(w['value']) and value_sets.get(w['value'])) for w in ws)
            if name in resets:
                # The reset proof licenses observed positive pairs, not an
                # absence/identity claim about a header that explicitly undefs.
                all_bad.add('<reset-undef>')
            macros[name] = dict(values=sorted(value_sets[name]), independent=not all_bad,
                                dependencies=sorted(all_deps), unrecognised=sorted(all_bad),
                                requirements=sorted(all_bad),
                                pairs={v: dict(independent=not (bad or invalid),
                                              dependencies=sorted(deps),
                                              requirements=sorted(bad | ({'<nonliteral-or-undef>'} if invalid else set())),
                                              unrecognised=sorted(bad | ({'<nonliteral-or-undef>'} if invalid else set())))
                                       for v, (deps, bad) in sorted(pairs.items())})
        results[root] = dict(macros=macros, taint=sorted(unknown), reset_prefixes=sorted(resets),
                            condition_macros={n: sorted(expand(n)) for n in sorted(condition_names & local.keys())
                                              if n in targets or n.startswith('__glibcxx_')})
    header_tests = {}
    for file, (ws, includes, _) in parsed.items():
        conditions = [c for w in ws for c in w['conditions']]
        conditions += [c for _, _, _, cs, _ in includes for c in cs or []]
        for c in conditions:
            for match in HEADER_TEST.finditer(c):
                predicate, operand = match.groups()
                key = header_test_key(predicate, operand, file)
                entry = header_tests.setdefault(key, dict(predicate=predicate, operand=operand, files=set()))
                entry['files'].add(file)
    header_tests = {k: dict(v, files=sorted(v['files'])) for k, v in sorted(header_tests.items())}
    result = dict(generator=VERSION, pure_candidates=sorted(PURE_CANDIDATES), roots=results,
                header_tests=header_tests,
                # Definitions and conditions live once in the full artifact.
                macros={n: ws for n, ws in sorted(writes.items()) if n in targets},
                support={n: ws for n, ws in sorted(writes.items()) if n in PURE_CANDIDATES or n.startswith('__glibcxx_')},
                guards=sorted(guards), includes=include_evidence, unparsed=unparsed,
                source_entry_absences=sorted(entry_absences),
                compiler_inputs={n: predefines.get(n) if isinstance(predefines, dict) else None
                                 for n in sorted(set(predefines) & compiler_inputs)},
                blocked_inputs=sorted(blocked))
    return apply_trust(result, trusted_inputs or {})
