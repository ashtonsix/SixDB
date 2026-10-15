#!/usr/bin/env python3
"""Resolve actual timed phases from frozen-source DWARF callsites, then audit CFG.
Usage: audit-dwarf.py ASSEMBLY_GZ ELF FROZEN_FORMATS_CPP OUTPUT
No phase assignment from clock ordinal or lambda numbering.
"""
import argparse,gzip,hashlib,json,re,subprocess,sys
from pathlib import Path
parser=argparse.ArgumentParser(__doc__);parser.add_argument('assembly',type=Path);parser.add_argument('binary',type=Path);parser.add_argument('source',type=Path);parser.add_argument('out',type=Path);parser.add_argument('--arms',type=int,nargs='+',default=[0,1,8,9]);parser.add_argument('--phases',nargs='+',default=['hit','miss','serial','update','erase']);parser.add_argument('--witness-kinds',type=int,nargs='+',default=[1]);args=parser.parse_args()
assembly,binary,source,out=args.assembly,args.binary,args.source,args.out;out.mkdir(parents=True,exist_ok=True)
phase_lines={}
for line,text in enumerate(source.read_text().splitlines(),1):
 for m in re.finditer(r'const auto\s+(construct|build|hit|miss|serial|update|erase|release)\s*=\s*format_time\b',text):phase_lines[line]=m[1]
assert set(phase_lines.values())=={'construct','build','hit','miss','serial','update','erase','release'}
text=gzip.open(assembly,'rt').read();heads=list(re.finditer(r'^([0-9a-f]+) <(.+)>:',text,re.M));funcs={int(m[1],16):(m[2],text[m.end():heads[i+1].start() if i+1<len(heads) else len(text)]) for i,m in enumerate(heads)};del text
pat=re.compile(r'^[ \t]*([0-9a-f]+):[ \t]+([a-zA-Z0-9_.]+)[ \t]*(.*?)$',re.M)
def parse(body):return [(int(m[1],16),m[2],m[3]) for m in pat.finditer(body)]
def target(arg):
 m=re.match(r'0x([0-9a-f]+)',arg);return int(m[1],16) if m else None
calls={'call','callq','bl','blr'}
def is_clock(o,t):return o in calls and 'steady_clock' in t
root_pattern=re.compile(r'void ht::format_trial<(\d+)u, \(ht::layout_solver::container\)(\d+), (\d+)u, (\d+)u>\(orbital::allocation::Pool&, unsigned int(?:, unsigned int)?\)')
root_functions=[];addresses=set()
for address,(name,body) in funcs.items():
 m=root_pattern.fullmatch(name)
 if not m:continue
 key=tuple(map(int,m.groups()))
 if key[3] not in args.arms:continue
 seq=parse(body);root_functions.append((address,key,name,seq))
 for a,o,t in seq:
  if is_clock(o,t) or (o in calls and 'format_time<' in t):addresses.add(a)
proc=subprocess.run(['llvm-addr2line-21','-a','-i','-e',str(binary)],input='\n'.join(hex(a) for a in sorted(addresses))+'\n',text=True,capture_output=True,check=True)
locations={};current=None
for line in proc.stdout.splitlines():
 if re.fullmatch(r'0x[0-9a-fA-F]+',line):current=int(line,16);locations[current]=[]
 elif current is not None:locations[current].append(line)
assert set(locations)==addresses
(out/'clock-callsites.json').write_text(json.dumps({hex(k):v for k,v in locations.items()},indent=2)+'\n')
def phase_at(address):
 found=[]
 for frame in locations[address]:
  m=re.search(r'/formats\.cpp:(\d+)(?:\D|$)',frame)
  if m and int(m[1]) in phase_lines:found.append(phase_lines[int(m[1])])
 unique=set(found)
 if len(unique)==1:return next(iter(unique))
 return None

def traverse(seq,start):
 positions={a:i for i,(a,o,t) in enumerate(seq)};seen=set();pending=[start];ends=set();unknown=[]
 while pending:
  i=pending.pop()
  if i in seen or i>=len(seq):continue
  a,o,t=seq[i]
  if is_clock(o,t):ends.add(a);continue
  seen.add(i)
  if o.startswith('ret') or o in ('ud2','br'):continue
  if o in calls and any(x in t for x in ['<abort@','<_Unwind_Resume@','<__clang_call_terminate','<__cxa_throw@','<__assert_fail@','__throw_','ThrowStd','<_ZSt9terminate@']):continue
  jump=o.startswith('j') or o=='b' or o.startswith('b.') or o in ('cbz','cbnz','tbz','tbnz')
  if jump:
   dest=target(t) if o not in ('cbz','cbnz','tbz','tbnz') else target(t.split(' <',1)[0].rsplit(', ',1)[-1])
   if dest in positions:pending.append(positions[dest])
   else:unknown.append(dict(address=hex(a),opcode=o,target=t))
   if o in ('jmp','jmpq','b'):continue
  pending.append(i+1)
 return [seq[i] for i in sorted(seen)],sorted(ends),unknown

def call_records(seq):return [dict(address=hex(a),opcode=o,target=t,indirect=o=='blr' or t.startswith('*')) for a,o,t in seq if o in calls]
def call_is_repeated(seq, call_address):
 # A true whole-loop helper is called outside the query loop. Do not infer
 # this from a format_trial lambda name: a per-key closure shares that name.
 positions={a:i for i,(a,o,t) in enumerate(seq)}
 start=positions[call_address]+1;pending=[start];seen=set()
 while pending:
  i=pending.pop()
  if i>=len(seq) or i in seen:continue
  seen.add(i);a,o,t=seq[i]
  if a==call_address:return True
  if o.startswith('ret') or o in ('ud2','br'):continue
  if o in calls and any(x in t for x in ['<abort@','<_Unwind_Resume@','<__clang_call_terminate','<__cxa_throw@','<__assert_fail@','__throw_','ThrowStd','<_ZSt9terminate@']):continue
  jump=o.startswith('j') or o=='b' or o.startswith('b.') or o in ('cbz','cbnz','tbz','tbnz')
  if jump:
   dest=target(t) if o not in ('cbz','cbnz','tbz','tbnz') else target(t.split(' <',1)[0].rsplit(', ',1)[-1])
   if dest in positions:pending.append(positions[dest])
   if o in ('jmp','jmpq','b'):continue
  pending.append(i+1)
 return False

def normalized(seq):
 positions={a:i for i,(a,o,t) in enumerate(seq)};lines=[]
 for a,o,t in seq:
  m=re.search(r'0x([0-9a-f]+) <(.+)>',t)
  if m:
   dest=int(m[1],16);ref='local:'+str(positions[dest]) if dest in positions else 'symbol:'+m[2];t=t[:m.start()]+ref+t[m.end():]
  if '%rip' in t and '#' in t:
   lhs,rhs=t.split('#',1);lhs=re.sub(r'-?0x[0-9a-f]+(?=\(%rip\))','DISP',lhs);t=lhs+'#'+rhs
  lines.append(o+' '+t)
 return hashlib.sha256('\n'.join(lines).encode()).hexdigest()

roots=[];callees={};fail=[]
for address,key,name,seq in root_functions:
 pos={a:i for i,(a,o,t) in enumerate(seq)};intervals={}
 for a,o,t in seq:
  if not is_clock(o,t):continue
  phase=phase_at(a)
  if phase not in tuple(args.phases):continue
  nodes,ends,unknown=traverse(seq,pos[a]+1)
  # All reachable ending clocks must have the same inline source assignment
  # as the start, including compiler-cloned end clocks on separate paths.
  # No clock is selected because of its ordinal in the root.
  if ends and all(phase_at(end)==phase for end in ends):
   intervals.setdefault(phase,[]).append((a,nodes,ends,unknown,name,None))
 for a,o,t in seq:
  if o not in calls or 'format_time<' not in t:continue
  phase=phase_at(a)
  if phase not in tuple(args.phases):
   if phase is None:fail.append(dict(key=key,reason='unresolved wrapper source phase',call=hex(a),frames=locations[a]))
   continue
  dest=target(t)
  if dest not in funcs:fail.append(dict(key=key,reason='missing wrapper body',target=t));continue
  wrapper,body=funcs[dest];ws=parse(body);wp={a:i for i,(a,o,t) in enumerate(ws)}
  before,first,unknown=traverse(ws,0)
  if len(first)!=1 or unknown:fail.append(dict(key=key,reason='nonunique wrapper start',phase=phase,first=first,unknown=unknown));continue
  nodes,ends,unknown=traverse(ws,wp[first[0]]+1)
  if len(ends)!=1:fail.append(dict(key=key,reason='nonunique wrapper end',phase=phase,ends=ends));continue
  intervals.setdefault(phase,[]).append((first[0],nodes,ends,unknown,wrapper,a))
 for phase in tuple(args.phases):
  found=intervals.get(phase,[])
  if len(found)!=1:fail.append(dict(key=key,reason='phase interval count',phase=phase,count=len(found)));continue
  clock,nodes,ends,unknown,symbol,wrapper_call=found[0];cs=call_records(nodes)
  for call in cs:call['inside_cfg_cycle']=call_is_repeated(nodes,int(call['address'],16))
  root=dict(K=key[0],kind=key[1],A=key[2],arm=key[3],phase=phase,symbol=symbol,clock=hex(clock),end=[hex(x) for x in ends],instructions=len(nodes),calls=cs,unknown_branches=unknown,phase_source_frames=locations[wrapper_call if wrapper_call else clock])
  if wrapper_call:root['wrapper_callsite']=hex(wrapper_call)
  roots.append(root)
  for c in cs:
   dest=target(c['target'])
   if dest in funcs and dest not in callees:
    n,b=funcs[dest];ins=parse(b);callees[dest]=dict(address=hex(dest),symbol=n,instructions=len(ins),calls=call_records(ins),normalized_sha256=normalized(ins))
  if key[0] in (16,40) and key[1] in args.witness_kinds and key[2]==10:
   prefix=f'K{key[0]}' + (f'-kind{key[1]}' if key[1]!=1 else '')
   (out/f'{prefix}-arm{key[3]}-{phase}.asm').write_text('Phase resolved from '+repr(root['phase_source_frames'])+'\nComplete timed CFG, address order.\n'+'\n'.join(f'{a:x}: {o} {t}' for a,o,t in nodes)+'\n')
   for c in cs:
    dest=target(c['target'])
    if dest in funcs and 'format_trial<' in funcs[dest][0]:
     n,b=funcs[dest];(out/f'{prefix}-arm{key[3]}-{phase}-helper.asm').write_text(hex(dest)+' <'+n+'>:'+b)
audit=dict(assembly_sha256=hashlib.sha256(assembly.read_bytes()).hexdigest(),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),phase_source_lines=phase_lines,timed_roots=roots,direct_callees=list(callees.values()),failures=fail)
(out/'codegen.json').write_text(json.dumps(audit,indent=2)+'\n')
print('Source/DWARF resolved',len(roots),'regions;',sum('wrapper_callsite' in r for r in roots),'outlined timing wrappers;',len(fail),'failures')
print('First failures:',fail[:8])
if fail:sys.exit(1)
