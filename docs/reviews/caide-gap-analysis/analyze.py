# SPDX-License-Identifier: GPL-3.0-or-later
import sys, re, csv, json, collections, hashlib, difflib, argparse
from pathlib import Path
sys.path.insert(0,str(Path('measurements/caide-gap/python').resolve()))
from tree_sitter import Language, Parser
import tree_sitter_cpp
P=Parser(Language(tree_sitter_cpp.language()))
ROOT=Path(sys.argv[1]) if len(sys.argv)>1 else Path('/home/appuser/.cache/caide-gap/caide2')
OUT=Path(sys.argv[2]) if len(sys.argv)>2 else Path('measurements/caide-gap')

def norm(s):
    s=s.replace(' ','')
    if '<deductionguidefor' in s:s=s[:s.index('<deductionguidefor')]+'@guide'
    elif s.endswith('::'):s+='@guide'
    if s.startswith('StaticAssert@'):s='@static_assert'
    # Remove template arguments, but preserve operator spellings.
    out=''; depth=0; i=0
    while i<len(s):
        c=s[i]
        if c=='<' and not out.endswith('operator') and not out.endswith('operator<'):
            depth+=1
        elif c=='>' and depth:depth-=1
        elif not depth:out+=c
        i+=1
    return out

def name(n):
    if not n:return ''
    if n.type in ['identifier','field_identifier','type_identifier','namespace_identifier','operator_name','destructor_name','qualified_identifier','template_function','template_type','dependent_name','operator_cast']:
        return n.text.decode()
    d=n.child_by_field_name('declarator')
    if d:return name(d)
    if n.type in ['reference_declarator','parenthesized_declarator'] and n.named_children:return name(n.named_children[0])
    if n.type=='structured_binding_declarator':return n.text.decode()
    return ''

def declarations(s):
    parse_s=re.sub(rb'if (?:!)?consteval',lambda m:(b'if (true)'+b' '*len(m[0]))[:len(m[0])],s)
    # Grammar-only view of conditional attributes: choose the GCC branch; offsets remain exact.
    def attr_branch(m):
        a=m[0];keep=m.span('keep');lo=keep[0]-m.start();hi=keep[1]-m.start()
        return re.sub(rb'[^\n]',b' ',a[:lo])+a[lo:hi]+re.sub(rb'[^\n]',b' ',a[hi:])
    parse_s=re.sub(rb'(?m)^#if(?:def)? [^\n]*_MSC_VER[^\n]*\n[ \t]*\[\[msvc::no_unique_address\]\]\n#else\n(?P<keep>[ \t]*\[\[no_unique_address\]\]\n)#endif',attr_branch,parse_s)
    parse_s=re.sub(rb'(?<=\w)\((min|max)\)(?=\()',lambda m:b' '+m[1]+b' ',parse_s)
    assert len(parse_s)==len(s)
    tree=P.parse(parse_s); out=[]; errors=[]
    def add(n,wrap,ctx,kind,nm):
        q=norm(nm)
        if not q:return None
        if not q.startswith('::') and ctx and not q.startswith(ctx+'::'):q=ctx+'::'+q
        q=q.lstrip(':')
        start=wrap.start_byte; end=wrap.end_byte
        # tree-sitter leaves the terminator outside a bare record/template.
        k=end
        while k<len(s) and s[k:k+1] in b' \t': k+=1
        if s[k:k+1]==b';':end=k+1
        dec=n.child_by_field_name('declarator') or n.child_by_field_name('name')
        while dec and dec.type not in ['identifier','field_identifier','type_identifier','operator_name','destructor_name','qualified_identifier','template_function','template_type','operator_cast']:
            dec=dec.child_by_field_name('declarator') or (dec.named_children[0] if dec.named_children and dec.type in ['reference_declarator','parenthesized_declarator'] else None)
        d=dict(name=q,kind=kind,start=start,end=end,line=n.start_point.row+1,nodekind=n.type,name_start=dec.start_byte if dec else -1,name_end=dec.end_byte if dec else -1)
        out.append(d); return d
    def walk(n,ctx='',record=False,wrap=None):
        typ=n.type
        if typ=='ERROR':errors.append((n.start_byte,n.end_byte,n.start_point.row+1))
        if typ=='template_declaration':
            for x in n.named_children:
                if x.type!='template_parameter_list' and x.type not in ['requires_clause','attribute_declaration']:
                    walk(x,ctx,record,wrap or n)
            return
        if typ in ['translation_unit','declaration_list','field_declaration_list','linkage_specification'] or typ.startswith('preproc_if') or typ in ['preproc_else','preproc_elif']:
            for x in n.named_children:walk(x,ctx,record)
            return
        if typ=='namespace_definition':
            nm=n.child_by_field_name('name'); scope=norm(nm.text.decode()) if nm else '(anonymous namespace)'
            q=ctx+'::'+scope if ctx else scope
            body=n.child_by_field_name('body')
            if body:walk(body,q,False)
            return
        if typ in ['struct_specifier','class_specifier','union_specifier','enum_specifier']:
            nm=n.child_by_field_name('name'); nm=nm.text.decode() if nm else ''
            d=add(n,wrap or n,ctx,'Enum' if typ=='enum_specifier' else 'Record',nm)
            if d and typ!='enum_specifier':
                body=n.child_by_field_name('body')
                if body:walk(body,d['name'],True)
            return
        if typ in ['declaration','field_declaration','function_definition','alias_declaration','type_definition','concept_definition','using_declaration','static_assert_declaration','namespace_alias_definition']:
            ty=n.child_by_field_name('type')
            # Record declarations often wrapped in a declaration.
            if ty and ty.type in ['struct_specifier','class_specifier','union_specifier','enum_specifier']:
                walk(ty,ctx,record,wrap or n);return
            nm=n.child_by_field_name('name')
            dec=n.child_by_field_name('declarator')
            nm=nm.text.decode() if nm else name(dec)
            kind='Var'
            if typ in ['alias_declaration','type_definition']:kind='Alias'
            elif typ=='concept_definition':kind='Concept'
            elif typ=='static_assert_declaration':nm='static_assert' if record else '@static_assert';kind='StaticAssert'
            elif typ=='using_declaration':nm=n.text.decode().strip(';');kind='Using'
            elif typ=='namespace_alias_definition':kind='NamespaceAlias'
            elif typ=='function_definition' or (dec and any(x.type=='function_declarator' for x in descendants(dec))):kind='Method' if record else 'Function'
            elif record:kind='Field' if 'static' not in n.text.decode().split('=')[0].split() else 'Var'
            if kind=='Function' and not ty and dec and any(x.type=='trailing_return_type' for x in descendants(dec)):
                kind='Guide';nm='@guide'
            declctx='' if kind=='StaticAssert' and not record else ctx
            if record and dec and re.search(rb'\bfriend\b',s[n.start_byte:dec.start_byte]):
                declctx='' if kind=='StaticAssert' and not record else ctx.rsplit('::',1)[0];kind='Function'
            add(n,wrap or n,declctx,kind,nm)
            # Only local record methods need independent discovery; ordinary locals belong to function.
            return
        if typ=='friend_declaration':
            for x in n.named_children:walk(x,ctx.rsplit('::',1)[0] if record and '::' in ctx else ctx,False,wrap or n)
            return
        # Error recovery: inspect child declarations, preserving unmatched status later.
        if typ=='ERROR':
            for x in n.named_children:walk(x,ctx,record)
    def descendants(n):
        yield n
        for x in n.named_children:yield from descendants(x)
    walk(tree.root_node)
    return out,errors

def d_report(path):
    raw={}; uniq={}; file_order={}
    for l in path.read_text().splitlines():
        m=re.match(r'family (\d+) "(.*)": (.*); (/.+):(\d+)$',l)
        if not m:continue
        i,n,r,f,ln=m.groups(); i=int(i);ln=int(ln)
        f=f.split('/atl/include/')[-1] if '/atl/include/' in f else f.split('/out/caide-compare/')[-1]
        file_order.setdefault(f,len(file_order))
        raw[i]=dict(id=i,name=norm(n),label=n,reason=r.split(';')[0],file=f,source_line=ln,kept=not r.startswith('removed:'),cause=int(re.search(r'cause=(\d+)',r).group(1)) if re.search(r'cause=(\d+)',r) else None)
    for i,d in raw.items():
        x=d; seen=set()
        while x['reason']=='family aggregation' and x['cause'] in raw and x['id'] not in seen:
            seen.add(x['id']);x=raw[x['cause']]
        d['effective']=x['reason'];d['order']=(file_order[d['file']],d['source_line'],i)
        key=(d['name'],d['file'],d['source_line'])
        if key not in uniq or uniq[key]['reason']=='family aggregation':uniq[key]=d
    return raw,list(uniq.values())

def ckind(s):
    if s=='CXXDeductionGuide':return 'Guide'
    if s in ['Function','FunctionTemplate']:return 'Function'
    if s in ['CXXMethod','CXXConstructor','CXXDestructor','CXXConversion']:return 'Method'
    if s in ['CXXRecord','Record','ClassTemplate','ClassTemplateSpecialization','ClassTemplatePartialSpecialization']:return 'Record'
    if s in ['Var','VarTemplate','VarTemplateSpecialization','VarTemplatePartialSpecialization']:return 'Var'
    if s in ['TypeAlias','TypeAliasTemplate','Typedef']:return 'Alias'
    if s=='Field':return 'Field'
    return s

def c_report(path):
    ds=[]
    for l in path.read_text().splitlines():
        m=re.match(r'(kept|removed)  (\w+) (.*?)  (.+):(\d+)  (.*)$',l)
        if m:
            state,k,n,f,ln,r=m.groups();n='@static_assert' if k=='StaticAssert' else n;ds.append(dict(name=norm(n),label=n,kind=ckind(k),raw_kind=k,line=int(ln),kept=state=='kept',reason=r))
    return ds

def group(r):
    if r.startswith('name:') or r.startswith('D3e rule 5:'):return 'name rule'
    if r.startswith('associated with'):return 'association'
    if r.startswith('AST:'):return 'AST'
    if r.startswith('dependent name:'):return 'dependent name'
    if 'ADL name' in r:return 'A6/ADL'
    if r.startswith('D3e rule 4:'):return 'A12 library-spelled member'
    if r.startswith('D3e rule 1:'):return 'D3e rule 1 kinds'
    if r=='never removed: operator':return 'never-removed operator'
    if r.startswith(('root:','instantiation effect:')):return 'roots/effects'
    if r.startswith('pinned:'):return 'pinned'
    return 'other'

rows=list(csv.DictReader((ROOT/'summary.tsv').open(),delimiter='\t')); rows_by={}
for r in rows:rows_by.setdefault(r['example'],{})[r['variant']]=r
selected={k:v for k,v in rows_by.items() if v['verify']['fallback']=='no' and v['verify']['run_check']=='ok'}

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    results=[]
    for ex,v in selected.items():
        folder=next((ROOT/'artifacts').glob('*/examples/'+ex))
        s=(folder/'distill-nlm.cpp').read_bytes(); c=(folder/'verify.cpp').read_bytes()
        for variant,buf in [('distill-nlm',s),('verify',c),('distill-compact',(folder/'distill-compact.cpp').read_bytes())]:assert len(buf)==int(v[variant]['bytes']),(ex,variant)
        raw,dr=d_report(folder/'distill-nlm.rep'); cr=c_report(folder/'verify.rep')
        dd,err=declarations(s);cd,cerr=declarations(c)
        # Map written distill declarations to kept report occurrences in source order, per qualified name.
        dg=collections.defaultdict(list); rg=collections.defaultdict(list)
        for d in dd:dg[d['name']].append(d)
        for r in dr:
            if r['kept']:rg[r['name']].append(r)
        for n,ds in dg.items():
            rs=sorted(rg[n],key=lambda x:x['order'])
            if len(ds)==len(rs):
                for d,r in zip(sorted(ds,key=lambda x:x['start']),rs):d['report']=r
        # Find caide inlined.cpp -> original-header line offsets using unique name anchors.
        cg=collections.defaultdict(list)
        for r in cr:
            if r['kind'] not in ['Namespace','TemplateTypeParm','NonTypeTemplateParm','TemplateTemplateParm','ParmVar']:cg[r['name']].append(r)
        offsets=collections.defaultdict(collections.Counter)
        for r in dr:
            cs=cg[r['name']]
            lines=sorted(set(x['line'] for x in cs))
            ds=[x for x in dr if x['name']==r['name']]
            if len(lines)==1 and len(ds)==1:offsets[r['file']][lines[0]-r['source_line']]+=1
        # Equal-size name buckets align written source occurrences directly (including removed rows).
        direct={}
        drg=collections.defaultdict(list)
        for r in dr:drg[r['name']].append(r)
        for n,rs in drg.items():
            lines=sorted(set(x['line'] for x in cg[n]))
            if len(lines)==len(rs):
                for r,line in zip(sorted(rs,key=lambda x:x['order']),lines):direct[(r['name'],r['file'],r['source_line'])]=line
        # Use anchor-supported exact coordinate matches. Multiple offsets handle includes/conditional elision.
        for d in dd:
            r=d.get('report')
            if not r:continue
            cs=[x for x in cg[r['name']] if x['kind']==d['kind'] or (d['kind']=='Method' and x['kind']=='Function')]
            offs=offsets[r['file']]
            target=direct.get((r['name'],r['file'],r['source_line']))
            candidates=[x for x in cs if (x['line']==target if target is not None else offs[x['line']-r['source_line']])]
            if candidates:
                score=max(offs[x['line']-r['source_line']] for x in candidates)
                candidates=[x for x in candidates if offs[x['line']-r['source_line']]==score]
                ls=set(x['line'] for x in candidates)
                if len(ls)==1:
                    d['caide']=candidates; d['caide_line']=next(iter(ls));d['caide_removed']=all(not x['kept'] for x in candidates);d['match_method']='ordinal source anchors' if target is not None else 'header offset anchors';d['source_offset']=d['caide_line']-r['source_line']
        results.append(dict(example=ex,bytes={k:int(v[k]['bytes']) for k in ['verify','distill-nlm','distill-compact']},dd=dd,cd=cd,dr=dr,cr=cr,errors=err,caide_errors=cerr,offsets={k:dict(v) for k,v in offsets.items()}))
    (OUT/'parsed.json').write_text(json.dumps(results))
    print('examples',len(results),'decls',sum(len(x['dd']) for x in results),'mapped',sum('report'in d for x in results for d in x['dd']),'caide matched',sum('caide'in d for x in results for d in x['dd']))
    print('errors',sum(len(x['errors']) for x in results),sum(len(x['caide_errors']) for x in results))
    print('unmatched',collections.Counter(d['name'] for x in results for d in x['dd'] if 'report'not in d).most_common(35))
    print('reasons',collections.Counter(d['report']['effective'] for x in results for d in x['dd'] if 'report'in d and d.get('caide_removed')).most_common(30))
if __name__=='__main__':main()
