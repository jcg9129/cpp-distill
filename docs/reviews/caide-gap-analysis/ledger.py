# SPDX-License-Identifier: GPL-3.0-or-later
from analyze import *
from branches import inactive_bytes
R=json.loads((OUT/'parsed.json').read_text())

def mask_ranges(buf,decls):
    # Smallest retained report occurrence owns a byte. Nested structural data/aliases remain in parent.
    owners=[-1]*len(buf)
    for i,d in sorted(enumerate(decls),key=lambda kv:kv[1]['end']-kv[1]['start'],reverse=True):
        for pos in range(d['start'],d['end']):owners[pos]=i
    # Directives are a separate ledger even when caide keeps them inside functions.
    pos=0; active=False
    for line in buf.splitlines(keepends=True):
        isdir=active or bool(re.match(rb'^\s*#',line))
        if isdir:
            owners[pos:pos+len(line)]=[-1]*len(line)
        active=isdir and bool(re.search(rb'\\[ \t]*\r?\n$',line))
        pos+=len(line)
    return owners

def owned(buf,own,i):return bytes(v for j,v in enumerate(buf) if own[j]==i)

def whitespace(buf):
    # Token lexer preserves literals, comments, preprocessor splice spelling; classify only lexically free trivia.
    token=re.compile(rb'[0-9][A-Za-z0-9_\x27.]*|R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"|(?:u8|u|U|L)?\'(?:\\[\s\S]|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/|\s+|[^\s]',re.M)
    code=bytearray();space=0
    for m in token.finditer(buf):
        if m[0].isspace():space+=len(m[0])
        else:code.extend(m[0])
    assert len(code)+space==len(buf)
    return bytes(code),space

allstats=collections.Counter(); internal_decls=[]; reverse_decls=[]; adecls=[]; details=[]; per=[]; unmatched=[]; remainder_text=[]; common_diffs=[]; directive_rows=[]
for x in R:
    ex=x['example'];folder=next((ROOT/'artifacts').glob('*/examples/'+ex))
    s=(folder/'distill-nlm.cpp').read_bytes();c=(folder/'verify.cpp').read_bytes()
    ds=[d for d in x['dd'] if 'report'in d]
    cs=x['cd']; cr=x['cr']
    cgroups=collections.defaultdict(list);cdgroups=collections.defaultdict(list)
    for r in cr:
        if r['kept']:cgroups[(r['name'],r['kind'])].append(r)
    for d in cs:cdgroups[(d['name'],d['kind'])].append(d)
    cbyline={}
    for key,decl in cdgroups.items():
        rs=cgroups[key];lines=sorted(set(r['line']for r in rs))
        if len(decl)==len(lines):
            for d,line in zip(sorted(decl,key=lambda d:d['start']),lines):cbyline[key+(line,)]=d
    # Attach fate/inline coordinates to structural subdeclarations missing from distill family rows.
    dg_all=collections.defaultdict(list);rg_all=collections.defaultdict(list)
    for d in x['dd']:dg_all[(d['name'],d['kind'])].append(d)
    for r in cr:rg_all[(r['name'],r['kind'])].append(r)
    nested_removed=[]
    for key,decls in dg_all.items():
        rs=rg_all[key];lines=sorted(set(r['line'] for r in rs))
        if len(lines)==len(decls):
            for d,line in zip(sorted(decls,key=lambda d:d['start']),lines):
                if 'report'not in d and all(not r['kept'] for r in rs if r['line']==line):
                    nested_removed.append(dict(**d,caide_line=line))
    # A redundant forward declaration can disappear despite a kept report row.
    for key,decl in cdgroups.items():
        lines=sorted(set(r['line']for r in cgroups[key]))
        if key==('atl::wavelet_matrix','Record') and len(decl)==2 and len(lines)==3:
            cbyline[key+(lines[-1],)]=max(decl,key=lambda d:d['start'])
    # Caide owners corresponding to distill families, not its separately indexed structural fields.
    cowners=[]; dpairs={}; used=set()
    for i,d in enumerate(ds):
        if 'caide_line'in d and not d['caide_removed']:
            k=(d['name'],d['kind'],d['caide_line']);cd=cbyline.get(k)
            if cd and (cd['start'],cd['end'])not in used:
                used.add((cd['start'],cd['end']));dpairs[i]=len(cowners);cowners.append(cd)
    so=mask_ranges(s,ds);co=mask_ranges(c,cowners)
    inactive=inactive_bytes(c)
    for pos,b in enumerate(inactive):
        if b:co[pos]=-1
    sb=[bytearray()for d in ds];cb=[bytearray()for d in cowners]
    for j,i in enumerate(so):
        if i>=0:sb[i].append(s[j])
    for j,i in enumerate(co):
        if i>=0:cb[i].append(c[j])
    stat=collections.Counter();stat['b:caide-only inactive branch text']=-sum(inactive);consumed_s=set();consumed_c=set()
    # Charge full owned spans of removed structural children to (d), ahead of residual whitespace.
    internal_mask=bytearray(len(s))
    nested_removed.sort(key=lambda d:-(d['end']-d['start']))
    for d in nested_removed:
        spans=[p for p in range(d['start'],d['end'])if so[p]in dpairs and not internal_mask[p]]
        if spans:
            for p in spans:internal_mask[p]=1
            internal_decls.append(dict(example=ex,bytes=len(spans),**d))
    # Assertions and non-function friends have no named distill family row of their own.
    def scan_inside(n):
        if n.type=='static_assert_declaration' or (n.type=='friend_declaration' and re.match(rb'friend\s+(?:class|struct)\s',n.text)):
            positions=[p for p in range(n.start_byte,n.end_byte)if so[p]in dpairs and not internal_mask[p]]
            if positions:
                owner=so[positions[0]];j=dpairs[owner]
                code,_=whitespace(s[n.start_byte:n.end_byte]);other,_=whitespace(bytes(cb[j]))
                if code not in other:
                    for p in positions:internal_mask[p]=1
                    internal_decls.append(dict(example=ex,bytes=len(positions),name=n.text.decode().split('(')[0],kind='StaticAssert' if n.type=='static_assert_declaration' else 'Friend',start=n.start_byte,end=n.end_byte,line=n.start_point.row+1))
        for z in n.named_children:scan_inside(z)
    scan_inside(P.parse(re.sub(rb'if (?:!)?consteval',lambda m:(b'if (true)'+b' '*len(m[0]))[:len(m[0])],s)).root_node)
    sb_rest=[bytearray()for d in ds]
    for pos,i in enumerate(so):
        if i>=0 and not internal_mask[pos]:sb_rest[i].append(s[pos])

    for i,d in enumerate(ds):
        if d.get('caide_removed'):
            r=d['report'];g=group(r['effective']);b=len(sb[i]);stat['a:'+g]+=b;consumed_s.add(i)
            adecls.append(dict(example=ex,group=g,bytes=b,**d))
        elif i in dpairs:
            j=dpairs[i];a=bytes(sb_rest[i]);b=bytes(cb[j]);ac,aw=whitespace(a);bc,bw=whitespace(b)
            stat['d:structural subdeclarations']+=len(sb[i])-len(sb_rest[i])
            stat['c:whitespace in matched declarations']+=aw-bw
            # Nonwhitespace deltas inside retained declaration bodies are d; exact unchanged code gives zero.
            stat['c:dangling attributes in caide']+=len(ac)-len(bc)
            consumed_s.add(i);consumed_c.add(j)
            assert ac.replace(b'[[nodiscard]]',b'')==bc.replace(b'[[nodiscard]]',b''),(ex,d['name'],ac,bc)
            if ac!=bc:common_diffs.append(dict(example=ex,name=d['name'],file=d['report']['file'],line=d['report']['source_line'],delta=len(ac)-len(bc),s=a.decode(),c=b.decode(),kind=d['kind']))
    # Countervailing caide-only declarations: source identity and distill removed fate.
    reverse_mask=bytearray(len(c))
    drg=collections.defaultdict(list)
    for r in x['dr']:drg[r['name']].append(r)
    for d in sorted(cs,key=lambda d:-(d['end']-d['start'])):
        rs=drg[d['name']]
        if not rs or not all(not r['kept'] for r in rs):continue
        positions=[p for p in range(d['start'],d['end'])if co[p]<0 and not reverse_mask[p] and not inactive[p]]
        if positions:
            for p in positions:reverse_mask[p]=1
            stat['offset:caide-only declarations']-=len(positions)
            reverse_decls.append(dict(example=ex,bytes=len(positions),**d))
    # Retained source directive accounting is independent of declaration ownership. No declaration should cross it.
    def directive_parts(buf,owners,consumed):
        parts=[];pos=0; lines=buf.splitlines(keepends=True);idx=0
        while idx<len(lines):
            line=lines[idx];start=pos;idx+=1;pos+=len(line)
            if re.match(rb'^\s*#',line):
                while re.search(rb'\\[ \t]*\r?\n$',line) and idx<len(lines):line=lines[idx];idx+=1;pos+=len(line)
                assert not any(owners[p]>=0 and owners[p]in consumed for p in range(start,pos)),(ex,start)
                text=buf[start:pos];parts.append((start,pos,text))
        return parts
    sd=directive_parts(s,so,consumed_s);cd=directive_parts(c,co,consumed_c)
    def category(text):
        if re.match(rb'\s*#\s*define\s+DISTILL_ONCE_',text):return 'generated once defines'
        if re.match(rb'\s*#\s*include',text):return 'includes'
        if re.match(rb'\s*#\s*(?:if|else|elif|endif)',text):return 'conditional directives'
        if re.match(rb'\s*#\s*line',text):return 'line markers'
        return 'other directives'
    for sign,parts in [(1,sd),(-1,cd)]:
        for lo,hi,t in parts:stat['b:'+category(t)]+=sign*len(t);directive_rows.append(dict(example=ex,sign=sign,category=category(t),bytes=len(t),text=t.decode()))
    # All unpaired or structural bytes remain explicit, with trivia separated from nonwhitespace text.
    def remainder(buf,owners,consumed,directives,extra_skip=None):
        skip=bytearray(extra_skip) if extra_skip is not None else bytearray(len(buf))
        for lo,hi,_ in directives:skip[lo:hi]=b'\1'*(hi-lo)
        return bytes(b for i,b in enumerate(buf)if not skip[i]and (owners[i]<0 or owners[i]not in consumed))
    sa=remainder(s,so,consumed_s,sd);ca=remainder(c,co,consumed_c,cd,bytearray(a or b for a,b in zip(reverse_mask,inactive)))
    sac,saw=whitespace(sa);cac,caw=whitespace(ca)
    remainder_text.append(dict(example=ex,s=sa.decode(),c=ca.decode(),s_nonwhite=len(sac),c_nonwhite=len(cac)))
    stat['c:whitespace outside matched declarations']+=saw-caw
    stat['c:unmatched and structural nonwhitespace']+=len(sac)-len(cac)
    assert sum(stat.values())==len(s)-len(c),(ex,stat,sum(stat.values()),len(s)-len(c))
    unmatched.extend(dict(example=ex,bytes=len(sb[i]),**d)for i,d in enumerate(ds)if i not in consumed_s)
    per.append(dict(example=ex,stats=dict(stat),caide_paired=len(dpairs),mapped=len(ds),extra=sum(d.get('caide_removed',False)for d in ds)))
    allstats.update(stat)
(OUT/'ledger.json').write_text(json.dumps(dict(totals=dict(allstats),extra=adecls,per=per,unmatched=unmatched,common_diffs=common_diffs,directives=directive_rows,internal=internal_decls,reverse=reverse_decls,remainder=remainder_text)))
print('TOTALS',json.dumps(dict(allstats),indent=2));print('SUM',sum(allstats.values()),'extra',len(adecls),'unmatched families',len(unmatched),'diffs',len(common_diffs))
print('unmatched',collections.Counter(d['name']for d in unmatched).most_common(20))
print('diff',[(x['example'],x['name'],x['delta'])for x in sorted(common_diffs,key=lambda x:abs(x['delta']),reverse=True)[:20]])
