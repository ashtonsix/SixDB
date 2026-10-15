#!/usr/bin/env python3
"""Extract the six K40/A10 MM update clock intervals and reachable native bodies.

Usage: clock_audit.py RECOVERED_WORKER_RESULTS IGNORED_OUTPUT_DIRECTORY
This is a bounded static audit, not executed-path counting or a cycle model.
"""
import re,gzip,sys,json
from pathlib import Path
src=Path(sys.argv[1]);out=Path(sys.argv[2]);job=src.parent.name;label=out.name;out.mkdir(parents=True,exist_ok=True)
text=gzip.open(src/'assembly.gz','rt').read();heads=list(re.finditer(r'^([0-9a-f]+) <(.+)>:',text,re.M));fs={int(m[1],16):(m[2],text[m.end():heads[i+1].start() if i+1<len(heads) else len(text)]) for i,m in enumerate(heads)};del text
pat=re.compile(r'^[ \t]*([0-9a-f]+):[ \t]+([a-zA-Z0-9_.]+)[ \t]*(.*?)$',re.M)
def parse(b):return [(int(m[1],16),m[2],m[3]) for m in pat.finditer(b)]
def dest(t):
 m=re.match(r'0x([0-9a-f]+)',t);return int(m[1],16) if m else None
calls={'call','callq','bl','blr'}
def walk(ins,start):
 pos={a:i for i,(a,o,t) in enumerate(ins)};seen=set();todo=[start];ends=set()
 while todo:
  i=todo.pop()
  if i in seen or i>=len(ins):continue
  a,o,t=ins[i]
  if o in calls and 'steady_clock' in t:ends.add(a);continue
  seen.add(i)
  if o.startswith('ret') or o in ('ud2','br'):continue
  if o in calls and any(x in t for x in ['<abort@','<_Unwind_Resume@','<__clang_call_terminate','<__cxa_throw@','<__assert_fail@','__throw_','ThrowStd','<_ZSt9terminate@']):continue
  jump=o.startswith('j') or o=='b' or o.startswith('b.') or o in ('cbz','cbnz','tbz','tbnz')
  if jump:
   d=dest(t) if o not in ('cbz','cbnz','tbz','tbnz') else dest(t.split(' <',1)[0].rsplit(', ',1)[-1])
   if d in pos:todo.append(pos[d])
   if o in ('jmp','jmpq','b'):continue
  todo.append(i+1)
 return [ins[i] for i in sorted(seen)],sorted(ends)
selected={};roots=[]
for arm in [0,1,2,3,4,5]:
 n=f'void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, {arm}u>(orbital::allocation::Pool&, unsigned int)'
 hits=[(a,b) for a,(name,b) in fs.items() if name==n];assert len(hits)==1
 a,b=hits[0];ins=parse(b);pos={a:i for i,(a,o,t) in enumerate(ins)};_,ends=walk(ins,0);assert len(ends)==1;c=ends[0]
 for i in range(11):
  nodes,ends=walk(ins,pos[c]+1);assert len(ends)==1,(arm,i,ends)
  if i==10:break
  c=ends[0]
 roots.append(dict(arm=arm,clock=hex(c),end=hex(ends[0]),calls=[dict(address=hex(a),op=o,target=t) for a,o,t in nodes if o in calls],instructions=len(nodes)))
 (out/f'arm{arm}-update.asm').write_text(n+'\n'+'\n'.join(f'{a:x}: {o} {t}' for a,o,t in nodes)+'\n')
 todo=[(dest(t),0) for a,o,t in nodes if o in calls]
 while todo:
  a,depth=todo.pop()
  if a in selected or a not in fs:continue
  name,b=fs[a]
  if not ('ht::' in name or 'ikea::streampack::' in name):continue
  if 'formats()' in name or name.startswith('void ht::format_trial'):continue
  seq=parse(b);cs=[dict(address=hex(x),op=o,target=t) for x,o,t in seq if o in calls or (o in ('jmp','jmpq','b') and dest(t) in fs)]
  selected[a]=dict(address=hex(a),symbol=name,instructions=len(seq),calls=cs,stack_prologue=[f'{x:x}: {o} {t}' for x,o,t in seq[:35] if ('sp' in t or 'x29' in t or 'x30' in t)])
  (out/f'callee-{a:x}.asm').write_text(name+'\n'+b)
  if depth<8:todo.extend((dest(x['target']),depth+1) for x in cs)
report=dict(label=label,job=job,roots=roots,callees=list(selected.values()))
(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
for r in roots:print('ROOT',r['arm'],r['clock'],r['instructions'],[(x['target'][:220]) for x in r['calls']])
for a,v in selected.items():print('BODY',hex(a),v['instructions'],v['symbol'][:180],v['stack_prologue'][:5],[(x['target'][:130]) for x in v['calls']][:20])
