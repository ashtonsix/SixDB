#!/usr/bin/env python3
"""Resolve actual timed phases from frozen-source DWARF callsites, then audit CFG.
Usage: audit-dwarf.py ASSEMBLY_GZ ELF COPIED_VALUE_WIDTH_BENCH_CPP OUTPUT
Requires the generated probe receipt and variant; updates retain reachable call boundaries.
No phase assignment from clock ordinal or lambda numbering.
"""
import argparse,gzip,hashlib,json,re,shlex,subprocess,sys
from pathlib import Path
parser=argparse.ArgumentParser(__doc__)
for name in ['assembly','binary','source','out']:parser.add_argument(name,type=Path)
parser.add_argument('--value-bytes',type=int,required=True)
parser.add_argument('--key-bits',type=int,default=40)
parser.add_argument('--probe-receipt',type=Path,required=True)
parser.add_argument('--variant',choices=['control','fused','partial'],required=True)
parser.add_argument('--arms',type=int,nargs='+',default=[0,1,2,3,4])
parser.add_argument('--phases',nargs='+',default=['hit','miss','serial','update'])
parser.add_argument('--witness-kinds',type=int,nargs='+',default=[1,2])
args=parser.parse_args()
assembly,binary,source,out=args.assembly,args.binary,args.source,args.out;out.mkdir(parents=True,exist_ok=True)
receipt=json.loads(args.probe_receipt.read_text())
matched=[r for r in receipt['commands'] if r['variant']==args.variant and r['width']==args.value_bytes and r['kind']=='bench']
assert len(matched)==1,('variant/width-specific probe receipt missing or ambiguous',len(matched))
entry=matched[0];tokens=entry['compile'];command=shlex.join(tokens)
assert hashlib.sha256(binary.read_bytes()).hexdigest()==entry['binary_sha256'], 'ELF differs from compiled probe receipt'
assert hashlib.sha256(source.read_bytes()).hexdigest()==entry['source_sha256'], 'copied translation unit differs'
compiled_source=Path(tokens[tokens.index('-c')+1])
assert compiled_source.name==source.name and compiled_source.parent.name==args.variant
linked_binary=Path(entry['link'][entry['link'].index('-o')+1])
assert linked_binary.name==binary.name and linked_binary.parent.name==args.variant
header_hashes={}
for name,field in [('promoted.h','promoted_sha256'),('record_table.h','record_table_sha256')]:
 if field not in entry:continue
 path=source.parent/name;digest=hashlib.sha256(path.read_bytes()).hexdigest()
 assert digest==entry[field],('generated header differs',name)
 header_hashes[name]=digest
def macro_value(name):
 explicit=[int(t.split('=',1)[1]) for t in tokens if t.startswith('-D'+name+'=')]
 if explicit:assert len(explicit)==1;return explicit[0]
 defaults=re.findall(r'^#define\s+'+re.escape(name)+r'\s+(\d+)\s*$',source.read_text(),re.M)
 assert len(defaults)==1,(name,'missing explicit or source-default macro')
 return int(defaults[0])
assert macro_value('HT_VALUE_BYTES')==args.value_bytes
assert macro_value('HT_VALUE_KEY')==args.key_bits
assert '-fno-vectorize' in tokens and '-fno-slp-vectorize' in tokens
phase_lines={}
for line,text in enumerate(source.read_text().splitlines(),1):
 for m in re.finditer(r'(?:const )?auto\s+(construct|build|hit|miss|serial|update|erase|release)\s*=\s*timed\b',text):phase_lines[line]=m[1]
assert set(phase_lines.values())=={'construct','build','hit','miss','serial','update','erase','release'}
text=gzip.open(assembly,'rt').read();heads=list(re.finditer(r'^([0-9a-f]+) <(.+)>:',text,re.M));funcs={int(m[1],16):(m[2],text[m.end():heads[i+1].start() if i+1<len(heads) else len(text)]) for i,m in enumerate(heads)};del text
pat=re.compile(r'^[ \t]*([0-9a-f]+):[ \t]+([a-zA-Z0-9_.]+)[ \t]*(.*?)$',re.M)
def parse(body):return [(int(m[1],16),m[2],m[3]) for m in pat.finditer(body)]
def target(arg):
 m=re.match(r'0x([0-9a-f]+)',arg);return int(m[1],16) if m else None
calls={'call','callq','bl','blr'}
def is_clock(o,t):return o in calls and 'steady_clock' in t
root_pattern=re.compile(r'void ht::trial<\(ht::layout_solver::container\)(\d+), (\d+)u, (\d+)u>\(orbital::allocation::Pool&, unsigned int, unsigned int\)')
root_functions=[];addresses=set()
for address,(name,body) in funcs.items():
 m=root_pattern.fullmatch(name)
 if not m:continue
 key=(args.key_bits,*map(int,m.groups()))
 if key[3] not in args.arms:continue
 seq=parse(body);root_functions.append((address,key,name,seq))
 for a,o,t in seq:
  if is_clock(o,t) or (o in calls and 'ht::timed<' in t):addresses.add(a)
proc=subprocess.run(['llvm-addr2line-21','--no-debuginfod','-a','-i','-e',str(binary)],input='\n'.join(hex(a) for a in sorted(addresses))+'\n',text=True,capture_output=True,check=True)
locations={};current=None
for line in proc.stdout.splitlines():
 if re.fullmatch(r'0x[0-9a-fA-F]+',line):current=int(line,16);locations[current]=[]
 elif current is not None:locations[current].append(line)
assert set(locations)==addresses
(out/'clock-callsites.json').write_text(json.dumps({hex(k):v for k,v in locations.items()},indent=2)+'\n')
def phase_at(address):
 found=[]
 for frame in locations[address]:
  m=re.search(r'/value_width_bench\.cpp:(\d+)(?:\D|$)',frame)
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
  if o=='br':unknown.append(dict(address=hex(a),opcode=o,target=t));continue
  if o.startswith('ret') or o=='ud2':continue
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
 # this from a trial lambda name: a per-key closure shares that name.
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

def external_branches(seq):
 positions={a for a,o,t in seq};result=[]
 for a,o,t in seq:
  jump=o.startswith('j') or o=='b' or o.startswith('b.') or o in ('cbz','cbnz','tbz','tbnz','br')
  if not jump:continue
  dest=target(t) if o not in ('cbz','cbnz','tbz','tbnz') else target(t.split(' <',1)[0].rsplit(', ',1)[-1])
  if dest not in positions:result.append(dict(address=hex(a),opcode=o,target=t,indirect=o=='br' or t.startswith('*')))
 return result

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
def capture_callee(dest):
 if dest in callees or dest not in funcs:return
 n,b=funcs[dest];ins=parse(b);cs=call_records(ins)
 for call in cs:call['inside_cfg_cycle']=call_is_repeated(ins,int(call['address'],16))
 callees[dest]=dict(address=hex(dest),symbol=n,instructions=len(ins),calls=cs,external_branches=external_branches(ins),normalized_sha256=normalized(ins))
# Preserve the reachable owner call graph for update, without assuming its
# allocations, slow branches or failure handlers execute on every operation.
update_pending=[]
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
  if o not in calls or 'ht::timed<' not in t:continue
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
  root=dict(K=key[0],value_bytes=args.value_bytes,kind=key[1],A=key[2],arm=key[3],phase=phase,symbol=symbol,clock=hex(clock),end=[hex(x) for x in ends],instructions=len(nodes),calls=cs,unknown_branches=unknown,phase_source_frames=locations[wrapper_call if wrapper_call else clock])
  if wrapper_call:root['wrapper_callsite']=hex(wrapper_call)
  roots.append(root)
  for c in cs:
   dest=target(c['target'])
   capture_callee(dest)
   if phase=='update' and dest in funcs:update_pending.append((dest,0))
  if key[1] in args.witness_kinds:
   prefix=f'K{key[0]}-W{args.value_bytes}-kind{key[1]}-A{key[2]}'
   (out/f'{prefix}-arm{key[3]}-{phase}.asm').write_text('Phase resolved from '+repr(root['phase_source_frames'])+'\nComplete timed CFG, address order.\n'+'\n'.join(f'{a:x}: {o} {t}' for a,o,t in nodes)+'\n')
   for c in cs:
    dest=target(c['target'])
    if dest in funcs and 'ht::trial<' in funcs[dest][0]:
     n,b=funcs[dest];(out/f'{prefix}-arm{key[3]}-{phase}-helper.asm').write_text(hex(dest)+' <'+n+'>:'+b)
visited_update=set();truncated=[]
while update_pending:
 dest,depth=update_pending.pop()
 if dest in visited_update:continue
 visited_update.add(dest);capture_callee(dest)
 n,b=funcs[dest]
 if 'ht::' not in n:continue
 if depth>=8 or len(visited_update)>512:
  truncated.append(dict(address=hex(dest),depth=depth,symbol=n));continue
 (out/f'update-callee-{dest:x}.asm').write_text(hex(dest)+' <'+n+'>:'+b)
 for call in callees[dest]['calls']:
  child=target(call['target'])
  if child in funcs:update_pending.append((child,depth+1))
expected={(args.key_bits,k,a,arm,phase) for k in [1,2] for a in [6,10] for arm in args.arms for phase in args.phases}
actual={(r['K'],r['kind'],r['A'],r['arm'],r['phase']) for r in roots}
if actual!=expected:fail.append(dict(reason='root coverage differs',missing=sorted(expected-actual),extra=sorted(actual-expected)))
audit=dict(variant=args.variant,probe_receipt_sha256=hashlib.sha256(args.probe_receipt.read_bytes()).hexdigest(),generated_header_sha256=header_hashes,update_callgraph_truncated=truncated,value_bytes=args.value_bytes,key_bits=args.key_bits,compile_command=command,assembly_sha256=hashlib.sha256(assembly.read_bytes()).hexdigest(),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),phase_source_lines=phase_lines,timed_roots=roots,direct_callees=list(callees.values()),failures=fail)
(out/'codegen.json').write_text(json.dumps(audit,indent=2)+'\n')
print('Source/DWARF resolved',len(roots),'regions;',sum('wrapper_callsite' in r for r in roots),'outlined timing wrappers;',len(fail),'failures')
print('First failures:',fail[:8])
if fail:sys.exit(1)
