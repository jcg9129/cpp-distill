# SPDX-License-Identifier: GPL-3.0-or-later
import re

def inactive_bytes(buf):
    """Analysis-only evaluation of the finite conditional vocabulary in caide artifacts.
    These facts agree with the distill CSES resolution records; no worker probe.
    """
    values={'__GNUC__':13};defined={'__GNUC__'};stack=[];active=True;mask=bytearray(len(buf));pos=0;directives=[]
    def cond(text):
        text=re.sub(r'/\*.*?\*/',' ',text)
        text=re.sub(r'defined\s*(?:\(\s*(\w+)\s*\)|(\w+))',lambda m:'1' if (m[1]or m[2])in defined else '0',text)
        text=re.sub(r'\b[A-Za-z_]\w*\b',lambda m:str(values.get(m[0],0)),text)
        assert re.fullmatch(r'[0-9()!&| \t]+',text),text
        text=text.replace('&&',' and ').replace('||',' or ');text=re.sub(r'!(?!=)',' not ',text)
        return bool(eval(text,{'__builtins__':{}},{}))
    for line in buf.splitlines(keepends=True):
        t=line.decode();m=re.match(r'\s*#\s*(\w+)\s*(.*)',t)
        if m:
            k,arg=m.groups();arg=arg.strip()
            if k in ['if','ifdef','ifndef']:
                test=cond(arg)if k=='if'else(arg in defined if k=='ifdef'else arg not in defined)
                stack.append([active,bool(test)]);active=active and test
            elif k=='elif':
                parent,taken=stack[-1];test=cond(arg);active=parent and not taken and test;stack[-1][1]=taken or test
            elif k=='else':
                parent,taken=stack[-1];active=parent and not taken;stack[-1][1]=True
            elif k=='endif':active=stack.pop()[0]
            elif k=='define' and active:
                q=re.match(r'(\w+)(?:\s+([01]))?$',arg)
                if q:defined.add(q[1]);values[q[1]]=int(q[2]or'1')
            elif k=='undef'and active:defined.discard(arg);values.pop(arg,None)
        elif not active:mask[pos:pos+len(line)]=b'\1'*len(line)
        pos+=len(line)
    assert not stack
    return mask
