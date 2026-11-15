# SPDX-License-Identifier: GPL-3.0-or-later
"""Profile argv validation mirrors core/command, with no response-file I/O."""
import re

MODELLED = {'-m32', '-m64', '-funsigned-char', '-fsigned-char', '-fshort-wchar',
            '-fno-exceptions', '-fno-rtti', '-fmodules', '-fopenmp'}
HARMLESS = {'-O0', '-O1', '-O2', '-O3', '-Os', '-Og', '-Ofast', '-g', '-g0', '-g1', '-g2', '-g3',
            '-w', '-pedantic', '-pedantic-errors', '-pthread', '-static', '-static-libstdc++',
            '-static-libgcc', '-s', '-pipe', '-lm', '-lpthread', '-lstdc++'}
REJECTED = {'-c', '-S', '-E', '-Xpreprocessor', '-Xlinker', '-Xassembler', '-M', '-MM', '--sysroot', '-specs', '--specs'}
REJECT_PREFIX = ('-include', '-imacros', '-Wp,', '-Wl,', '-Wa,', '-B', '-isysroot', '--sysroot=',
                 '-save-temps', '-specs=', '--specs=', '-fplugin=')
VALUE_PREFIX = ('-march=', '-mtune=', '-mcpu=', '-fdiagnostics-color=', '-fmax-errors=', '-fsanitize=')
IDENTIFIER = r'[A-Za-z_][A-Za-z_0-9]*'


def operations(flags, inert=()):
    if not isinstance(flags, list) or any(not isinstance(f, str) or not f or any(ord(c) < 32 or ord(c) == 127 for c in f) for f in flags):
        raise ValueError('flags must be an array of non-empty argv strings')
    result, unknown = [], set()
    i = 0
    while i < len(flags):
        f = flags[i]
        i += 1
        if f.startswith('-std='):
            raise ValueError('-std= belongs in std')
        if f in REJECTED or f.startswith(REJECT_PREFIX) or f.startswith('@'):
            raise ValueError('rejected option: ' + f)
        if f.startswith(('-D', '-U')):
            op, value = f[:2], f[2:]
            if not value:
                if i == len(flags):
                    raise ValueError('missing macro option argument')
                value = flags[i]
                i += 1
            if op == '-U':
                if not re.fullmatch(IDENTIFIER, value):
                    raise ValueError('invalid -U name')
                result.append((value, None))
            else:
                spelling, sep, replacement = value.partition('=')
                # A definition may have an ordinary function-like signature.
                if not re.fullmatch(IDENTIFIER + r'(?:\((?:[A-Za-z_0-9., \t]*)\))?', spelling):
                    raise ValueError('invalid -D definition')
                result.append((spelling.split('(')[0], '#define ' + spelling + ' ' + (replacement if sep else '1')))
        elif f in MODELLED | HARMLESS or re.fullmatch(r'-W[a-z0-9][a-z0-9+=_-]*', f) or any(f.startswith(p) and len(f) > len(p) for p in VALUE_PREFIX):
            pass
        elif f.startswith(('-I', '-L', '-l', '-o', '-MF', '-MT', '-isystem', '-iquote')) or f in {'-MD', '-MMD'} or not f.startswith('-'):
            raise ValueError('option belongs outside profile compile flags: ' + f)
        elif f in inert:
            unknown.add(f)
        else:
            raise ValueError('unmodelled option: ' + f)
    if set(inert) != unknown or len(inert) != len(set(inert)):
        raise ValueError('inert must name distinct unmodelled options present in flags')
    return result
