# SPDX-License-Identifier: GPL-3.0-or-later
from analyze import *
R=json.loads((OUT/'parsed.json').read_text());L=json.loads((OUT/'ledger.json').read_text())
result=[]
# Raw lexical occurrences, excluding literals/comments. Macro text and arguments are included.
tok=re.compile(rb'[0-9][A-Za-z0-9_\x27.]*|R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"|(?:u8|u|U|L)?\'(?:\\[\s\S]|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/|[A-Za-z_]\w*',re.M)
for x in R:
 ex=x['example'];s=next((ROOT/'artifacts').glob('*/examples/'+ex+'/distill-nlm.cpp')).read_bytes()
 occ=collections.defaultdict(list)
 for m in tok.finditer(s):
  if re.fullmatch(rb'[A-Za-z_]\w*',m[0]):occ[m[0].decode()].append(m.start())
 declared={d['name_start']:d for d in x['dd']if d['kind']=='Method'}
 for d in L['extra']:
  if d['example']!=ex or d['group']!='name rule' or d['kind']!='Method':continue
  simple=d['name'].split('::')[-1];own=[z for z in x['dd']if z['name']==d['name']]
  external=[pos for pos in occ[simple] if not any(z['start']<=pos<z['end'] for z in own)]
  if external and all(pos in declared and declared[pos]['name'].rsplit('::',1)[0]!=d['name'].rsplit('::',1)[0]for pos in external):
   result.append(dict(example=ex,name=d['name'],file=d['report']['file'],line=d['report']['source_line'],bytes=d['bytes'],witnesses=[declared[pos]['name']for pos in external]))
(OUT/'candidates.json').write_text(json.dumps(result))
print('foreign method declarators only',len(result),sum(d['bytes']for d in result))
for d in sorted(result,key=lambda d:d['bytes'],reverse=True)[:15]:print(d)
# Positive syntactic whitelist candidate: empty tag classes with a first,
# in-class explicitly defaulted default constructor. Measure a raw-name closure,
# preserving any object/type whose spelling remains outside the deletion set.
tag_names={'atl::commutative_tags','atl::commutative_fold','atl::commutative_tags_t','atl::commutative_fold_t','atl::commutative_tags_t::commutative_tags_t','atl::commutative_fold_t::commutative_fold_t'}
tag_results=[]
for x in R:
 ex=x['example'];s=next((ROOT/'artifacts').glob('*/examples/'+ex+'/distill-nlm.cpp')).read_bytes()
 pool=[d for d in L['extra']if d['example']==ex and d['name']in tag_names]
 units=[d for d in pool if d['kind']!='Method'];deleted=list(units)
 while True:
  mask=bytearray(len(s))
  for d in deleted:mask[d['start']:d['end']]=b'\1'*(d['end']-d['start'])
  external=collections.Counter(m[0].decode()for m in tok.finditer(s)if re.fullmatch(rb'[A-Za-z_]\w*',m[0])and not mask[m.start()])
  new=[d for d in deleted if not external[d['name'].split('::')[-1]]]
  if len(new)==len(deleted):break
  deleted=new
 for d in pool:
  if any(u['start']<=d['start']and d['end']<=u['end']for u in deleted):tag_results.append(d)
(OUT/'tag-candidates.json').write_text(json.dumps(tag_results))
print('tag raw-name closure',len(tag_results),sum(d['bytes']for d in tag_results))
