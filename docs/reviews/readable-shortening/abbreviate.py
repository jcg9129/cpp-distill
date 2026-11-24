"""Deterministic, spelling-wide readable abbreviation for the analysis only."""
from collections import Counter
from itertools import count
from pathlib import Path
import re
import sys

# Import, rather than fork, the earlier lexer, exclusions, and line accounting.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'minify-analysis'))
import measure as baseline


ABBREVIATIONS = dict(
    index='idx', count='cnt', number='num', value='val', vertex='vtx',
    vertices='vtxs', length='len', position='pos', element='elem', iterator='it',
    pointer='ptr', temporary='tmp', string='str', integer='int', polynomial='poly',
    matrix='mat', modulus='mod', segment='seg', minimum='min', maximum='max',
    distance='dist', parent='par', previous='prev', current='cur', result='res',
    buffer='buf', source='src', destination='dst', read='rd', write='wr',
    input='in', output='out', query='qry', update='upd', answer='ans',
    argument='arg', parameter='param', function='func', operation='op',
    operator='oper', compare='cmp', comparator='cmp', difference='diff',
    capacity='cap', size='sz', height='ht', weight='wt', depth='dep',
    child='ch', children='chs', left='lft', right='rgt', middle='mid',
    prefix='pref', suffix='suff', vector='vec', degree='deg', coefficient='coef',
    numerator='num', denominator='den', inverse='inv', multiplicity='mult',
    initialize='init', identity='ident',
)

# Compiler-driven lookup, independently of which headers happened to be reached.
PROTOCOLS = frozenset('''
promise_type get_return_object initial_suspend final_suspend return_void
return_value yield_value unhandled_exception await_transform await_ready
await_suspend await_resume get_return_object_on_allocation_failure
coroutine_traits coroutine_handle tuple_size tuple_element get value type begin end
'''.split())

CHUNKS = re.compile(r'_+|[0-9]+|[A-Za-z]+')
WORDS = re.compile(r'[A-Z]+(?=[A-Z][a-z]|$)|[A-Z]?[a-z]+|[A-Z]+')


def parts(name):
    """Keep digit and underscore runs verbatim; split letter runs at case edges."""
    result = []
    for chunk in CHUNKS.findall(name):
        result.extend(WORDS.findall(chunk) if chunk.isalpha() else [chunk])
    assert ''.join(result) == name
    return result


def word_abbreviation(word):
    if len(word) <= 3:
        return word
    lower = word.lower()
    new = ABBREVIATIONS.get(lower)
    if new is None:
        new = (lower[0] + re.sub('[aeiou]', '', lower[1:]))[:4]
    if word.isupper():
        return new.upper()
    if word[0].isupper():
        return new.capitalize()
    return new


def candidates(name):
    """Base abbreviation, then one-letter-longer prefixes, left-to-right rounds.

    A word's next length is restored from its original spelling. This also works
    for conventional abbreviations such as vertices -> vtxs (not a subsequence).
    The fully restored spelling precedes the decimal-suffix fallback.
    """
    original = parts(name)
    short = [word_abbreviation(p) if p.isalpha() else p for p in original]
    yield ''.join(short)
    while short != original:
        for i, word in enumerate(original):
            if short[i] != word:
                short[i] = word[:len(short[i]) + 1]
                yield ''.join(short)
    for digit in count(1):
        yield name + str(digit)


def mapping_for(tokens, declared, kept, predefined=()):
    counts = Counter(t.text for t in tokens if t.kind == 'id')
    eligible = ({n for n in declared if baseline.IDENT.fullmatch(n) and len(n) > 4}
                & counts.keys()) - kept - set(predefined) - PROTOCOLS
    # Reserve even original spellings scheduled for renaming: deterministic and
    # conservative, with no swaps or reclamation of recognizable original names.
    blocked = set(counts) | kept | set(predefined) | PROTOCOLS | baseline.KEYWORDS
    used, mapping, collisions = set(), {}, {}
    for old in sorted(eligible, key=lambda n: (-counts[n], n)):
        steps = 0
        for new in candidates(old):
            if ((new == old or new not in blocked) and new not in used
                    and '__' not in new and not re.match(r'_[A-Z]', new)):
                break
            steps += 1
        used.add(new)
        if new != old:
            mapping[old] = new
        if steps:
            collisions[old] = steps
    # Injective over *all* source identifier spellings, not just mapped names.
    assert len({mapping.get(n, n) for n in counts}) == len(counts)
    assert not (set(mapping.values()) & (kept | set(predefined) | PROTOCOLS | baseline.KEYWORDS))
    return mapping, counts, collisions


def collapse_spaces(source):
    """Collapse ASCII spaces in inter-token gaps only, within physical lines.

    Literal/header/comment bytes, tabs, line endings and continuations stay exact.
    No run crosses a token, so object/function macro adjacency stays unchanged.
    """
    parts_, end = [], 0
    for token in baseline.lex(source):
        parts_.append(re.sub(' {2,}', ' ', source[end:token.start]))
        parts_.append(source[token.start:token.end])
        end = token.end
    parts_.append(re.sub(' {2,}', ' ', source[end:]))
    return ''.join(parts_)
