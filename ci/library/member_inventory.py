# SPDX-License-Identifier: GPL-3.0-or-later
"""Raw preprocessing-token inventory; no preprocessing or configuration filtering.

Like the worker's raw lexer, retain directives and inactive branches, skip
comments/literals/pp-numbers, and decode UCNs and phase-2 line splices. This is
independent of the feature-macro audit (which intentionally masks more tokens).
"""
import re
import unicodedata
from bisect import bisect_right


UCN = r'\\(?:u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|u\{[0-9a-fA-F]+\}|N\{[^}\n]+\})'
ASCII_IDENTIFIER = re.compile(r'[A-Za-z_$][A-Za-z_0-9$]*')
RAW = re.compile(r'(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(')
QUOTED = re.compile(r'''(?:u8|u|U|L)?(?:"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')''', re.S)
SPLICE = re.compile(r'\\[ \t]*(?:\r\n|\n|\r)')
NEWLINE = re.compile(r'\r\n|\n\r|\r')
ESCAPE = re.compile(UCN)


def normalize(name):
    def decode(match):
        token = match[0]
        if token.startswith('\\N{'):
            return unicodedata.lookup(token[3:-1])
        value = int(token[3:-1] if token.startswith('\\u{') else token[2:], 16)
        if 0xD800 <= value <= 0xDFFF:
            raise ValueError('surrogate in header identifier')
        return chr(value)
    return ESCAPE.sub(decode, name)


def unit(text, i):
    escape = ESCAPE.match(text, i) if text[i] == '\\' else None
    return (normalize(escape[0]), escape.end()) if escape else (text[i], i + 1)


def identifier_continue(character):
    return character == '$' or ('a' + character).isidentifier()


def identifier(text, i):
    begin = i
    ascii_name = ASCII_IDENTIFIER.match(text, i)
    parts = [ascii_name[0]] if ascii_name else []
    if ascii_name:
        i = ascii_name.end()
    while i < len(text):
        character, end = unit(text, i)
        allowed = identifier_continue(character) if i != begin else character == '$' or character.isidentifier()
        if not allowed:
            break
        parts.append(character)
        i = end
    return ''.join(parts), i


def number_end(text, i):
    # Consume complete pp-numbers, including digit separators and UDL suffixes.
    if not (text[i] in '0123456789' or
            (text[i] == '.' and i + 1 < len(text) and text[i + 1] in '0123456789')):
        return i
    i += 1
    while i < len(text):
        character, end = unit(text, i)
        if identifier_continue(character) or character == '.':
            i = end
        elif character in '+-' and text[i - 1] in 'eEpP':
            i = end
        elif character == "'" and end < len(text) and identifier_continue(unit(text, end)[0]):
            i = end
        else:
            break
    return i


def tokens(text):
    physical = NEWLINE.sub('\n', text)
    logical_positions, physical_ends, removed = [], [], [0]
    for splice in SPLICE.finditer(physical):
        logical_positions.append(splice.start() - removed[-1])
        physical_ends.append(splice.end())
        removed.append(removed[-1] + splice.end() - splice.start())
    text = SPLICE.sub('', physical)
    names, pasting, i = set(), False, 0
    while i < len(text):
        if text[i].isspace():
            i += 1
            continue
        if text.startswith('//', i):
            end = text.find('\n', i + 2)
            i = len(text) if end < 0 else end + 1
            continue
        if text.startswith('/*', i):
            end = text.find('*/', i + 2)
            if end < 0:
                raise ValueError('unterminated header comment')
            i = end + 2
            continue
        raw = RAW.match(text, i)
        quoted = QUOTED.match(text, i) if not raw else None
        if raw or quoted:
            if raw:
                # Phase-2 transformations are reverted inside raw literals.
                # Search the physical body so a spliced near-delimiter cannot
                # end the literal early and expose its contents as names.
                quote, paren = raw.end() - len(raw[1]) - 2, raw.end() - 1
                quote += removed[bisect_right(logical_positions, quote)]
                paren += removed[bisect_right(logical_positions, paren)]
                if physical[quote + 1:paren] != raw[1]:
                    raise ValueError('invalid raw header delimiter')
                end = physical.find(')' + raw[1] + '"', paren + 1)
                if end < 0:
                    raise ValueError('unterminated raw header literal')
                end += len(raw[1]) + 2
                i = end - removed[bisect_right(physical_ends, end)]
            else:
                i = quoted.end()
            if i < len(text):
                _, i = identifier(text, i)
            continue
        end = number_end(text, i)
        if end != i:
            i = end
            continue
        name, end = identifier(text, i)
        if name:
            names.add(name)
            i = end
            continue
        if text.startswith(('##', '%:%:'), i):
            pasting = True
            i += 4 if text.startswith('%:%:', i) else 2
            continue
        if text[i] in '\"\'':
            raise ValueError('unterminated header literal')
        i += 1
    return names, pasting


def inventory(files):
    names, pasting_headers = set(), []
    for header, text in sorted(files.items()):
        try:
            identifiers, pasting = tokens(text)
        except (ValueError, KeyError) as error:
            raise ValueError(f'{header}: {error}') from error
        names.update(identifiers)
        if pasting:
            pasting_headers.append(header)
    return dict(names=sorted(names), reachable_headers=len(files),
                token_pasting=bool(pasting_headers), token_pasting_headers=pasting_headers)


def validate_names(names):
    return (isinstance(names, list) and all(isinstance(n, str) and n and
            identifier(n, 0) == (n, len(n)) and '\\' not in n for n in names)
            and names == sorted(set(names)))


def validate_inventory(record):
    return (isinstance(record, dict) and validate_names(record.get('names')) and
            type(record.get('reachable_headers')) is int and record['reachable_headers'] >= 0 and
            type(record.get('token_pasting')) is bool and
            isinstance(record.get('token_pasting_headers'), list) and
            all(isinstance(h, str) and h for h in record['token_pasting_headers']) and
            record['token_pasting_headers'] == sorted(set(record['token_pasting_headers'])) and
            record['token_pasting'] == bool(record['token_pasting_headers']) and
            len(record['token_pasting_headers']) <= record['reachable_headers'])
