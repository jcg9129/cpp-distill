# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent corpus checks for the report-only analysis; no compiler invocation."""
from analyze import *
from branches import inactive_bytes
R=json.loads((OUT/'parsed.json').read_text());L=json.loads((OUT/'ledger.json').read_text())
assert len(R)==len(selected)==100
assert len(rows_by)==126 and len(rows)==504
assert {k:sum(x['bytes'][k]for x in R)for k in ['verify','distill-nlm','distill-compact']}=={'verify':1692146,'distill-nlm':2431626,'distill-compact':2159753}
assert sum(L['totals'].values())==739480
assert len(L['extra'])==2905 and sum(x['bytes']for x in L['extra'])==544160
assert sum(x['bytes']for x in L['internal'])==26998
assert len(L['internal'])==344
assert len(L['reverse'])==83 and sum(x['bytes']for x in L['reverse'])==19636
assert all(not x['errors'] and not x['caide_errors'] for x in R)
assert sum('report'in d for x in R for d in x['dd'])==8791
assert sum('caide'in d for x in R for d in x['dd'])==8790
candidate=json.loads((OUT/'candidates.json').read_text())
assert len(candidate)==6 and sum(d['bytes']for d in candidate)==2018
tags=json.loads((OUT/'tag-candidates.json').read_text())
assert len(tags)==152 and sum(d['bytes']for d in tags)==6560
assert all(sum(x['stats'].values())==next(y for y in R if y['example']==x['example'])['bytes']['distill-nlm']-next(y for y in R if y['example']==x['example'])['bytes']['verify']for x in L['per'])
# The unmatched nonwhitespace declaration text cancels exactly, example by example.
for x in L['remainder']:
 def without_shells(t):
  t=re.sub(r'namespace\s+[\w:]+\s*\{','',t).replace('}','')
  return re.sub(r'\s+','',t)
 assert without_shells(x['s'])==without_shells(x['c']),x['example']

# Reproduce whitespace compaction independently, protecting literals/comments and
# complete spliced logical lines, then require byte equality with the CI artifact.
literal=re.compile(rb'[0-9][A-Za-z0-9_\x27.]*|(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"|(?:u8|u|U|L)?\'(?:\\[\s\S]|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/',re.M)
def compact(s):
 protect=bytearray(len(s))
 for m in literal.finditer(s):
  if not m[0][:1].isdigit():protect[m.start():m.end()]=b'\1'*(m.end()-m.start())
 lines=s.splitlines(keepends=True);pos=0;logical_start=0;spliced=False
 for line in lines:
  pos+=len(line)
  cont=bool(re.search(rb'\\[ \t\r\v\f]*\n$',line));spliced=spliced or cont
  if not cont:
   if spliced:protect[logical_start:pos]=b'\1'*(pos-logical_start)
   logical_start=pos;spliced=False
 out=bytearray();pos=0
 for line in lines:
  a=pos;b=pos+len(line)-(1 if line.endswith(b'\n')else 0);end=b
  while a<b and s[a]in b' \t\v\f\r'and not protect[a]:a+=1
  while b>a and s[b-1]in b' \t\v\f\r'and not protect[b-1]:b-=1
  if a!=b or (end<len(s)and protect[end]):
   out.extend(s[a:b])
   if end<len(s):out.extend(b'\n')
  pos+=len(line)
 return bytes(out)
repairs=0;guard_bytes=0;guard_count=0
for x in R:
 folder=next((ROOT/'artifacts').glob('*/examples/'+x['example']))
 for variant in ['verify','noverify','distill-nlm','distill-compact']:
  assert len((folder/(variant+'.cpp')).read_bytes())==int(rows_by[x['example']][variant]['bytes'])
 for variant in ['distill-nlm','distill-compact']:
  report=(folder/(variant+'.rep')).read_text()
  assert report.startswith('distill: wrote pruned ')
  assert not re.search(r'(?m)^.*#(?:if|elif|else|endif): kept',report)
 assert 'whitespace compacted:'in(folder/'distill-compact.rep').read_text()
 s=(folder/'distill-nlm.cpp').read_bytes();c=(folder/'distill-compact.cpp').read_bytes()
 assert compact(s)==c,x['example']
 repairs+='optimized after'in(folder/'verify.rep').read_text()
 assert not re.search(rb'(?m)^\s*#\s*line\b',s)
 guards=re.findall(rb'(?m)^#define (DISTILL_ONCE_\w+)\n',s)
 for g in guards:
  assert len(re.findall(rb'\b'+g+rb'\b',s))==1
  guard_bytes+=len(b'#define '+g+b'\n');guard_count+=1
assert repairs==11
assert (guard_count,guard_bytes)==(1187,45106)
print('PASS: 100 ledgers; 400 sizes; 200 error-free syntax views; 100 exact compaction reproductions; 11 repair examples included; 1,187 guard definitions have no surviving user-text use.')
print('summary.tsv sha256:',hashlib.sha256((ROOT/'summary.tsv').read_bytes()).hexdigest())
