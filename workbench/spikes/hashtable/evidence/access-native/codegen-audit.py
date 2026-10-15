#!/usr/bin/env python3
"""Static complete timed-region call audit; not a performance model."""
import re,sys,gzip,json,hashlib,collections
from pathlib import Path
src,out=map(Path,sys.argv[1:]);job=src.parent.name;label=job;out.mkdir(parents=True,exist_ok=True)
text=gzip.open(src/'assembly.gz','rt').read();heads=list(re.finditer(r'^([0-9a-f]+) <(.+)>:',text,re.M));funcs={int(m[1],16):(m[2],text[m.end():heads[i+1].start() if i+1<len(heads) else len(text)]) for i,m in enumerate(heads)};del text
pattern=re.compile(r'^[ \t]*([0-9a-f]+):[ \t]+([a-zA-Z0-9_.]+)[ \t]*(.*?)$',re.M)
def parse(body):return [(int(m[1],16),m[2],m[3]) for m in pattern.finditer(body)]
def target(arg):
 m=re.match(r'0x([0-9a-f]+)',arg);return int(m[1],16) if m else None
def normalized(ins):
 positions={a:i for i,(a,o,t) in enumerate(ins)};lines=[]
 for a,o,t in ins:
  m=re.search(r'0x([0-9a-f]+) <(.+)>',t)
  if m:
   dest=int(m[1],16);ref='local:'+str(positions[dest]) if dest in positions else 'symbol:'+m[2]
   t=t[:m.start()]+ref+t[m.end():]
  if '%rip' in t and '#' in t:
   lhs,rhs=t.split('#',1);lhs=re.sub(r'-?0x[0-9a-f]+(?=\(%rip\))','DISP',lhs);t=lhs+'#'+rhs
  lines.append(o+' '+t)
 return hashlib.sha256('\n'.join(lines).encode()).hexdigest()
calls={'call','callq','bl','blr'}
def call_records(nodes):return [dict(address=hex(a),opcode=o,target=t,indirect=o=='blr' or t.startswith('*')) for a,o,t in nodes if o in calls]
def traverse(ins,start,stop_clock=True):
 positions={a:i for i,(a,o,t) in enumerate(ins)};seen=set();pending=[start];ends=set();unknown=[]
 while pending:
  i=pending.pop()
  if i in seen or i>=len(ins):continue
  a,o,t=ins[i]
  if stop_clock and o in calls and 'steady_clock' in t:ends.add(a);continue
  seen.add(i)
  if o.startswith('ret') or o in ('ud2','br'):continue
  if o in calls and any(x in t for x in ['<abort@','<_Unwind_Resume@','<__clang_call_terminate','<__cxa_throw@','<__assert_fail@','__throw_', 'ThrowStd', '<_ZSt9terminate@']):continue
  jump=o.startswith('j') or o=='b' or o.startswith('b.') or o in ('cbz','cbnz','tbz','tbnz')
  if jump:
   dest=target(t) if o not in ('cbz','cbnz','tbz','tbnz') else target(t.split(' <',1)[0].rsplit(', ',1)[-1])
   if dest in positions:pending.append(positions[dest])
   else:unknown.append(dict(address=hex(a),opcode=o,target=t))
   if o in ('jmp','jmpq','b'):continue
  pending.append(i+1)
 return [ins[i] for i in sorted(seen)],sorted(ends),unknown
roots=[];callees={};fail=[]
for address,(name,body) in funcs.items():
 match=re.fullmatch(r'void ht::format_trial<(\d+)u, \(ht::layout_solver::container\)(\d+), (\d+)u, (\d+)u>\(orbital::allocation::Pool&, unsigned int\)',name)
 if not match:continue
 key=tuple(map(int,match.groups()));ins=parse(body);pos={a:i for i,(a,o,t) in enumerate(ins)}
 _,first,_=traverse(ins,0)
 if len(first)!=1:fail.append(dict(key=key,reason='nonunique first clock',clocks=first));continue
 intervals=[];clock=first[0];seen=set()
 while clock not in seen:
  seen.add(clock);nodes,next_clocks,unknown=traverse(ins,pos[clock]+1);intervals.append((clock,nodes,next_clocks,unknown))
  if len(intervals)>=13:break
  if not next_clocks:break
  if len(next_clocks)!=1:fail.append(dict(key=key,reason='nonunique successor clock',clock=hex(clock),next=next_clocks));break
  clock=next_clocks[0]
 plans=[(4,'hit'),(6,'miss'),(8,'serial'),(12,'erase')]
 timers=[target(t) for a,o,t in ins if o in calls and 'format_time<' in t and "'lambda3'()" in t]
 all_timers=[t for a,o,t in ins if o in calls and 'format_time<' in t]
 if len(all_timers)!=len(timers):fail.append(dict(key=key,reason='unexpected outlined timer',timers=all_timers));continue
 if timers:
  if len(timers)!=1:fail.append(dict(key=key,reason='outlined timer lookup'));continue
  tn,tb=funcs[timers[0]];ti=parse(tb);tp={a:i for i,(a,o,t) in enumerate(ti)}
  _,tf,_=traverse(ti,0);assert len(tf)==1
  ns,es,us=traverse(ti,tp[tf[0]]+1);cs=call_records(ns)
  roots.append(dict(K=key[0],kind=key[1],A=key[2],arm=key[3],phase='serial',symbol=tn,clock=hex(tf[0]),end=[hex(x) for x in es],instructions=len(ns),calls=cs,unknown_branches=us,outlined_timer=True))
  plans=[(4,'hit'),(6,'miss'),(10,'erase')]
 elif len(intervals)<13:fail.append(dict(key=key,reason='insufficient clocks through erase',count=len(intervals)));continue
 for number,phase in plans:
  clock,nodes,ends,unknown=intervals[number];cs=call_records(nodes)
  roots.append(dict(K=key[0],kind=key[1],A=key[2],arm=key[3],phase=phase,symbol=name,clock=hex(clock),end=[hex(x) for x in ends],instructions=len(nodes),calls=cs,unknown_branches=unknown))
  for c in cs:
   dest=target(c['target'])
   if dest in funcs and dest not in callees:
    n,b=funcs[dest];seq=parse(b);callees[dest]=dict(address=hex(dest),symbol=n,instructions=len(seq),calls=call_records(seq),normalized_sha256=normalized(seq))
  if key[0]==40 and key[2]==10 and key[3] in range(10):
   (out/f'K{key[0]}-kind{key[1]}-arm{key[3]}-{phase}.asm').write_text('Complete CFG between clock '+hex(clock)+' and '+','.join(hex(x) for x in ends)+'. Address order.\n'+'\n'.join(f'{a:x}: {o} {t}' for a,o,t in nodes)+'\n')
audit=dict(job=job,assembly_sha256=hashlib.sha256((src/'assembly.gz').read_bytes()).hexdigest(),timed_roots=roots,direct_callees=list(callees.values()),failures=fail)
(out/'codegen.json').write_text(json.dumps(audit,indent=2)+'\n')
print(label,'roots',len(roots),'callees',len(callees),'failures',len(fail),'indirect',sum(c['indirect'] for r in roots for c in r['calls']))
print('first failures',fail[:6])
for phase in ['hit','miss','serial','erase']:
 print(phase,'callful roots',sum(bool(r['calls']) for r in roots if r['phase']==phase),'direct callee names',[v['symbol'][:220] for a,v in list(callees.items())[:3]])
