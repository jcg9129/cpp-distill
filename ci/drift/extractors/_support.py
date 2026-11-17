# SPDX-License-Identifier: GPL-3.0-or-later
"""Small HTML tree and strict field helpers. No fetched content is executed."""
from html.parser import HTMLParser
import json
import re

from evidence import EvidenceUnavailable, ExtractorFailure, FetchFailure


def one(items, field):
    items = list(items)
    if len(items) != 1:
        raise ExtractorFailure(f'{field}: expected one match, got {len(items)}')
    return items[0]


def present(value, field):
    if not isinstance(value, str) or not value.strip():
        raise ExtractorFailure(f'missing {field}')
    return value


def match(pattern, value, field):
    found = re.fullmatch(pattern, present(value, field))
    if not found:
        raise ExtractorFailure(f'unsupported {field}: {value}')
    return found


class Node:
    def __init__(self, tag='', attrs=()):
        self.tag, self.attrs, self.children = tag, dict(attrs), []

    def all(self, tag=None):
        for child in self.children:
            if isinstance(child, Node):
                if tag is None or child.tag == tag:
                    yield child
                yield from child.all(tag)

    def raw(self):
        if self.tag in ('script', 'style'):
            return ''
        if self.tag == 'br':
            return '\n'
        return ''.join(c.raw() if isinstance(c, Node) else c for c in self.children)

    def text(self):
        return ' '.join(self.raw().split())

    def cells(self):
        return [c for c in self.children if isinstance(c, Node) and c.tag in ('td', 'th')]


class Tree(HTMLParser):
    void = {'area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source', 'wbr'}

    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.root = Node()
        self.stack = [self.root]
        self.feed(text)
        self.close()

    def handle_starttag(self, tag, attrs):
        # The CSES table omits some end tags. Do not cross a nested table
        # boundary while handling the optional row/cell end tags.
        if tag in ('tr', 'td', 'th'):
            for i in range(len(self.stack) - 1, 0, -1):
                old = self.stack[i].tag
                if old == 'table' or (tag != 'tr' and old == 'tr'):
                    break
                if old == tag or (tag in ('td', 'th') and old in ('td', 'th')):
                    del self.stack[i:]
                    break
        node = Node(tag, attrs)
        self.stack[-1].children.append(node)
        if tag not in self.void:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in self.void:
            self.handle_endtag(tag)

    def handle_endtag(self, tag):
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                return

    def handle_data(self, data):
        self.stack[-1].children.append(data)


def decode(payload, metadata):
    headers = {k.lower(): v for k, v in metadata.get('headers', {}).items()}
    charset = re.search(r'charset=["\']?([\w-]+)', headers.get('content-type', ''), re.I)
    encoding = charset[1] if charset else 'utf-8-sig'
    try:
        return payload.decode(encoding).replace('\r\n', '\n').replace('\r', '\n')
    except (UnicodeError, LookupError) as error:
        raise ExtractorFailure('invalid source encoding') from error


def html(payload, metadata):
    status = metadata.get('status', 200)
    if status in (403, 408, 429, 503):
        raise EvidenceUnavailable(f'HTTP {status}')
    root = Tree(decode(payload, metadata)).root
    headings = [n.text().casefold() for tag in ('title', 'h1', 'h2') for n in root.all(tag)]
    markers = ('just a moment', 'access denied', 'verify you are human', 'checking your browser',
               'security check', 'login challenge', 'sign in to continue', 'consent required',
               'attention required', 'ddos-guard', 'captcha', 'before you continue')
    if any('too many requests' in h or 'rate limit' in h for h in headings):
        raise EvidenceUnavailable('rate limit page despite HTTP 200')
    if any(any(s in h for s in markers) or h in ('login', 'log in', 'sign in', 'consent') for h in headings):
        raise FetchFailure('login/challenge/consent page despite HTTP 200')
    if any(n.attrs.get('id', '').startswith(('cf-chl-', 'challenge-form', 'consent-form')) for n in root.all()):
        raise FetchFailure('challenge/consent form despite HTTP 200')
    if any(n.attrs.get('type', '').lower() == 'password' for n in root.all('input')):
        raise FetchFailure('login form despite HTTP 200')
    return root


def json_page(payload, metadata):
    # JSON endpoints sometimes redirect to an HTML login/challenge page.
    text = decode(payload, metadata)
    html(payload, metadata)
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ExtractorFailure('duplicate JSON field: ' + key)
            result[key] = value
        return result
    value = json.loads(text, object_pairs_hook=unique)
    if not isinstance(value, dict):
        raise ExtractorFailure('expected JSON object')
    message = str(value.get('message', value.get('detail', ''))).casefold()
    if 'rate limit' in message or 'too many requests' in message:
        raise EvidenceUnavailable(message)
    if 'login' in message or 'unauthorized' in message or 'challenge' in message or 'consent' in message:
        raise FetchFailure(message)
    return value


def command(text):
    """Lex one displayed command, retaining quote spelling within macro tokens."""
    text = present(text, 'compile command').strip()
    args, offset = [], 0
    token = re.compile(r'''(?:[^\s'"\\]+|\\.|"(?:[^"\\]|\\.)*"|'[^']*')+''')
    while offset < len(text):
        if text[offset].isspace():
            offset += 1
            continue
        found = token.match(text, offset)
        if not found:
            raise ExtractorFailure('unsupported command quoting')
        args.append(found[0])
        offset = found.end()
    return args


def gcc_version(value):
    # Only the documented GCC release labels omit a zero patch component.
    match(r'\d+\.\d+(?:\.\d+)?', value, 'GCC version')
    return value if value.count('.') == 2 else value + '.0'


def record(language_id, version, args, runtime=(), libraries=(), label=None, family='gcc'):
    value = dict(language_id=present(language_id, 'language ID/label'), compiler_family=family,
                 compiler_version=present(version, 'compiler version'), compile_arguments=args,
                 runtime_notes=[present(s, 'runtime note') for s in runtime],
                 library_notes=[present(s, 'library note') for s in libraries])
    if label is not None:
        value['language_label'] = present(label, 'language label')
    return value


def section(root, tag, prefix):
    """Select a heading's following siblings, ending at the next same heading."""
    heading = one((n for n in root.all(tag) if n.text().startswith(prefix)), prefix)
    parent = one((n for n in [root, *root.all()] if heading in n.children), 'heading parent')
    result = Node('section')
    for child in parent.children[parent.children.index(heading) + 1:]:
        if isinstance(child, Node) and child.tag == tag:
            break
        result.children.append(child)
    return heading, result
