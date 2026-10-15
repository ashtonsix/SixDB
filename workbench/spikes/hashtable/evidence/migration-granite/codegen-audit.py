#!/usr/bin/env python3
"""Reconstruct complete read-interval fusion evidence from a recovered worker dump.

Usage: python3 codegen-audit.py JOB_ID LABEL, from the repository root.
Inputs are build/workers/JOB_ID/results/assembly.gz. Output goes to ignored
build/hashtable-design-packets/migration-audit/LABEL. This is static control-flow
inspection, not a performance model. The native archive supplies the binary,
full assembly and frozen sources needed to inspect every recorded address.
"""
import re,sys,gzip,json,hashlib
from pathlib import Path
job=sys.argv[1]; label=sys.argv[2]; base=Path('build/workers')/job/'results'; out=Path('build/hashtable-design-packets/migration-audit')/label;out.mkdir(parents=True,exist_ok=True)
s=gzip.open(base/'assembly.gz','rt').read(); heads=list(re.finditer(r'^([0-9a-f]+) <(.+)>:',s,re.M)); funcs={int(h[1],16):(h[2],s[h.end():heads[i+1].start() if i+1<len(heads) else len(s)]) for i,h in enumerate(heads)}
inst=re.compile(r'^[ \t]*([0-9a-f]+):[ \t]+([a-zA-Z0-9_.]+)[ \t]*(.*?)$',re.M)
call={'call','callq','bl','blr'}
def target(arg):
 m=re.match(r'0x([0-9a-f]+)',arg);return int(m[1],16) if m else None
def parse(body):return [(int(m[1],16),m[2],m[3]) for m in inst.finditer(body)]
def calllist(ins):return [dict(address=hex(a),opcode=o,target=arg) for a,o,arg in ins if o in call]
reads={}
for addr,(name,body) in funcs.items():
 if name.startswith('auto void ht::migration_trial<') and "(auto const&)" in name and '::operator()<ht::promoted_table<' in name:
  reads[addr]=(name,body,'serial' if "::'lambda0'" in name else 'hit')
 elif name.startswith('void ht::migration_trial<8u, (ht::layout_solver::container)0, 3u, 1u>') and any(x in name for x in ["::'lambda5'()","::'lambda6'()"]):
  reads[addr]=(name,body,'serial' if "::'lambda6'" in name else 'hit')
audit={'job':job,'assembly_sha256':hashlib.sha256((base/'assembly.gz').read_bytes()).hexdigest(),'candidate_read_roots':[],'timed_roots':[]}
for addr,(name,body,phase) in reads.items():
 ins=parse(body); cs=calllist(ins)
 audit['candidate_read_roots'].append(dict(address=hex(addr),symbol=name,phase=phase,instructions=len(ins),calls=cs,indirect_calls=[c for c in cs if c['opcode']=='blr' or c['target'].startswith('*')]))
 if 'migration_trial<40u,' in name and ', 9u, 5u>(' in name:
  kind=re.search(r'container\)(\d)',name)[1];(out/f'candidate-k40-kind{kind}-{phase}.asm').write_text(hex(addr)+' <'+name+'>:\n'+body)
for addr,(name,body) in funcs.items():
 if not name.startswith('void ht::migration_trial<') or 'bool)::' in name:continue
 ins=parse(body); pos={a:i for i,(a,o,t) in enumerate(ins)}
 for i,(start,op,arg) in enumerate(ins):
  if op not in call or 'steady_clock' not in arg:continue
  visited=set();pending=[i+1];edges=[]
  while pending:
   j=pending.pop()
   if j in visited or j>=len(ins):continue
   a,o,t=ins[j]
   if o in call and 'steady_clock' in t:edges.append(a);continue
   visited.add(j)
   if o.startswith('ret') or o in ('ud2','br'):continue
   jump=o.startswith('j') or o=='b' or o.startswith('b.') or o in ('cbz','cbnz','tbz','tbnz')
   if jump:
    dest=target(t) if o not in ('cbz','cbnz','tbz','tbnz') else target(t.split(' <',1)[0].rsplit(', ',1)[-1])
    if dest in pos:pending.append(pos[dest])
    if o in ('jmp','jmpq','b'):continue
   pending.append(j+1)
  reached=[ins[j] for j in sorted(visited)]
  rs={reads[target(t)][2] for a,o,t in reached if o in call and target(t) in reads}
  if not rs:continue
  if any(target(t) not in reads for a,o,t in reached if o in call):continue
  cs=calllist(reached);audit['timed_roots'].append(dict(symbol=name,start_clock=hex(start),stop_clocks=[hex(a) for a in sorted(set(edges))],phases=sorted(rs),instructions=len(reached),calls=cs,indirect_calls=[c for c in cs if c['opcode']=='blr' or c['target'].startswith('*')]))
  if 'migration_trial<40u,' in name and ', 9u, 5u>(' in name:
   kind=re.search(r'container\)(\d)',name)[1]
   (out/f'timed-k40-kind{kind}-{next(iter(rs))}.asm').write_text('CFG reachable from clock '+hex(start)+' until next clock; both baseline arms and candidate helper call; address order.\n'+'\n'.join(f'{a:x}: {o} {t}' for a,o,t in reached)+'\n')
starts={(r['start_clock'],tuple(r['phases'])) for r in audit['timed_roots']}
audit['timed_roots']=[r for r in audit['timed_roots'] if not any((c,tuple(r['phases'])) in starts for c in r['stop_clocks'])]
(out/'codegen.json').write_text(json.dumps(audit,indent=2)+'\n')
print(label,'candidate roots',len(reads),'timed roots',len(audit['timed_roots']),'candidate indirect',sum(len(x['indirect_calls']) for x in audit['candidate_read_roots']),'timed indirect',sum(len(x['indirect_calls']) for x in audit['timed_roots']))
from collections import Counter
print('timed call families',Counter('candidate_helper' if target(c['target']) in reads else c['target'].split(' <',1)[-1] for r in audit['timed_roots'] for c in r['calls']))
print('candidate non-format calls',Counter(c['target'].split(' <',1)[-1] for r in audit['candidate_read_roots'] for c in r['calls'] if not any(x in c['target'] for x in ['formats()','__cxa_guard','__cxa_atexit','_Unwind_Resume'])))
