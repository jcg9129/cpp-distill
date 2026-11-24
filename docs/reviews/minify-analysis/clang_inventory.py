"""Small ctypes binding to the installed libclang; no third-party Python packages."""
import ctypes as C
import ctypes.util
import os
import re


class String(C.Structure):
    _fields_ = [('data', C.c_void_p), ('flags', C.c_uint)]


class Cursor(C.Structure):
    _fields_ = [('kind', C.c_int), ('xdata', C.c_int), ('data', C.c_void_p * 3)]


class Location(C.Structure):
    _fields_ = [('data', C.c_void_p * 2), ('value', C.c_uint)]


class Range(C.Structure):
    _fields_ = [('data', C.c_void_p * 2), ('begin', C.c_uint), ('end', C.c_uint)]


class LexToken(C.Structure):
    _fields_ = [('data', C.c_uint * 4), ('ptr', C.c_void_p)]


def raw_tokens(path, offsets=False):
    """Independent Clang raw lexer for the main file (no header traversal)."""
    lib = C.CDLL(ctypes.util.find_library('clang-22') or 'libclang-22.so.1')
    def bind(name, result, params):
        f = getattr(lib,name)
        f.restype, f.argtypes = result, params
        return f
    create = bind('clang_createIndex',C.c_void_p,[C.c_int,C.c_int])
    parse = bind('clang_parseTranslationUnit',C.c_void_p,
                 [C.c_void_p,C.c_char_p,C.POINTER(C.c_char_p),C.c_int,C.c_void_p,C.c_uint,C.c_uint])
    args = [b'-x',b'c++',b'-std=c++23',b'-nostdinc']
    argv = (C.c_char_p*len(args))(*args)
    index = create(0,0)
    # SingleFileParse skips all included headers. Tokenization does not require
    # a semantically valid TU and remains available after missing-type errors.
    tu = parse(index,os.fsencode(path),argv,len(args),None,0,0x400)
    if not tu:
        raise RuntimeError('raw lexer parse failed: '+str(path))
    file = bind('clang_getFile',C.c_void_p,[C.c_void_p,C.c_char_p])(tu,os.fsencode(path))
    location = bind('clang_getLocationForOffset',Location,[C.c_void_p,C.c_void_p,C.c_uint])
    extent = bind('clang_getRange',Range,[Location,Location])(
        location(tu,file,0),location(tu,file,os.path.getsize(path)))
    tokens, count = C.POINTER(LexToken)(), C.c_uint()
    bind('clang_tokenize',None,[C.c_void_p,Range,C.POINTER(C.POINTER(LexToken)),C.POINTER(C.c_uint)])(
        tu,extent,C.byref(tokens),C.byref(count))
    kind = bind('clang_getTokenKind',C.c_int,[LexToken])
    token_location = bind('clang_getTokenLocation',Location,[C.c_void_p,LexToken])
    spelling_location = bind('clang_getSpellingLocation',None,
                             [Location,C.c_void_p,C.c_void_p,C.c_void_p,C.POINTER(C.c_uint)])
    spelling = bind('clang_getTokenSpelling',String,[C.c_void_p,LexToken])
    cstr = bind('clang_getCString',C.c_char_p,[String])
    dispose_string = bind('clang_disposeString',None,[String])
    result = []
    for n in range(count.value):
        s = spelling(tu,tokens[n])
        token_kind, text = kind(tokens[n]), cstr(s).decode('latin1')
        # CIndex returns an unclean spelling when a token starts at a splice.
        # Canonicalize phase 2 outside literal bodies; keep literal bytes exact.
        original_text = text
        text = re.sub(r'^(?:\\[ \t\v\f]*\r?\n)+','',text)
        leading = len(original_text)-len(text)
        if token_kind != 3:
            text = re.sub(r'\\[ \t\v\f]*\r?\n','',text)
        token = (token_kind,text)
        if offsets:
            offset = C.c_uint()
            spelling_location(token_location(tu,tokens[n]),None,None,None,C.byref(offset))
            token += (offset.value+leading,)
        result.append(token)
        dispose_string(s)
    bind('clang_disposeTokens',None,[C.c_void_p,C.POINTER(LexToken),C.c_uint])(tu,tokens,count)
    bind('clang_disposeTranslationUnit',None,[C.c_void_p])(tu)
    bind('clang_disposeIndex',None,[C.c_void_p])(index)
    return result


def inventory(path, args, library=None):
    lib = C.CDLL(library or ctypes.util.find_library('clang-22') or 'libclang-22.so.1')
    def bind(name, restype, argtypes):
        f = getattr(lib, name)
        f.restype, f.argtypes = restype, argtypes
        return f
    create = bind('clang_createIndex', C.c_void_p, [C.c_int, C.c_int])
    parse = bind('clang_parseTranslationUnit', C.c_void_p,
                 [C.c_void_p, C.c_char_p, C.POINTER(C.c_char_p), C.c_int,
                  C.c_void_p, C.c_uint, C.c_uint])
    root = bind('clang_getTranslationUnitCursor', Cursor, [C.c_void_p])
    visitor = C.CFUNCTYPE(C.c_uint, Cursor, Cursor, C.c_void_p)
    visit = bind('clang_visitChildren', C.c_uint, [Cursor, visitor, C.c_void_p])
    loc = bind('clang_getCursorLocation', Location, [Cursor])
    spelling_loc = bind('clang_getExpansionLocation', None,
                        [Location, C.POINTER(C.c_void_p), C.POINTER(C.c_uint),
                         C.POINTER(C.c_uint), C.POINTER(C.c_uint)])
    filename = bind('clang_getFileName', String, [C.c_void_p])
    spelling = bind('clang_getCursorSpelling', String, [Cursor])
    cstr = bind('clang_getCString', C.c_char_p, [String])
    dispose_string = bind('clang_disposeString', None, [String])
    is_decl = bind('clang_isDeclaration', C.c_uint, [C.c_int])
    def string(s):
        b = cstr(s)
        out = b.decode('utf8') if b else ''
        dispose_string(s)
        return out
    index = create(0, 0)
    argv = (C.c_char_p * len(args))(*(a.encode() for a in args))
    tu = parse(index, os.fsencode(path), argv, len(args), None, 0, 0)
    if not tu:
        raise RuntimeError(f'libclang failed to parse {path}')
    count = bind('clang_getNumDiagnostics', C.c_uint, [C.c_void_p])
    diagnostic = bind('clang_getDiagnostic', C.c_void_p, [C.c_void_p, C.c_uint])
    severity = bind('clang_getDiagnosticSeverity', C.c_int, [C.c_void_p])
    diag_spelling = bind('clang_getDiagnosticSpelling', String, [C.c_void_p])
    dispose_diag = bind('clang_disposeDiagnostic', None, [C.c_void_p])
    errors = []
    for i in range(count(tu)):
        d = diagnostic(tu, i)
        if severity(d) >= 3:
            errors.append(string(diag_spelling(d)))
        dispose_diag(d)
    if errors:
        raise RuntimeError(f'{path}: ' + '\n'.join(errors))
    names, declarations, files = set(), [], {}
    source = os.path.realpath(path)
    @visitor
    def collect(cur, parent, data):
        f = C.c_void_p()
        offset = C.c_uint()
        spelling_loc(loc(cur), C.byref(f), None, None, C.byref(offset))
        if f.value:
            if f.value not in files:
                files[f.value] = os.path.realpath(string(filename(f)))
            if files[f.value] != source:
                return 1  # skip complete system declaration subtree
        if is_decl(cur.kind):
            name = string(spelling(cur))
            if name:
                names.add(name)
                declarations.append([name, offset.value, cur.kind])
        return 2  # recurse
    visit(root(tu), collect, None)
    include_visitor = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(Location), C.c_uint, C.c_void_p)
    includes = set()
    @include_visitor
    def include(f, stack, length, data):
        name = os.path.realpath(string(filename(f)))
        if name != source:
            includes.add(name)
    bind('clang_getInclusions', None, [C.c_void_p, include_visitor, C.c_void_p])(tu, include, None)
    bind('clang_disposeTranslationUnit', None, [C.c_void_p])(tu)
    bind('clang_disposeIndex', None, [C.c_void_p])(index)
    return {'declared': sorted(names), 'declarations': declarations, 'headers': sorted(includes)}
