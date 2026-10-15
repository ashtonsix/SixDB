#!/usr/bin/env python3
"""Audit frozen selector-mix-v1 timed read/update CFGs using actual DWARF callsites.
All five cases, all18 arms and source-defined13-phase histories; six reads and
update are audited. No phase assignment from clock ordinal or lambda number.
"""
import argparse,gzip,hashlib,json,re,shlex,subprocess,sys
from pathlib import Path
parser=argparse.ArgumentParser(__doc__)
for name in ['assembly','binary','source','out']:parser.add_argument(name,type=Path)
parser.add_argument('--value-bytes',type=int,choices=[0,2,8,16],required=True)
parser.add_argument('--key-bits',type=int,choices=[16,40,64],required=True)
parser.add_argument('--kind',type=int,choices=[0,1,2],required=True)
parser.add_argument('--compile-commands',type=Path,required=True)
parser.add_argument('--selector-mix-variant',choices=['fold','multiply'],required=True)
args=parser.parse_args();kind=args.kind
assert (args.key_bits,args.value_bytes,kind) in [(16,2,1),(40,0,0),(40,2,1),(40,8,2),(64,16,1)],'unsupported selector-mix case'
args.arms=list(range(18));args.phases=['hit','miss','ordered' if kind==0 else 'serial','update','aged_hit','aged_miss','reused_hit'];args.witness_kinds=[kind]
assembly,binary,source,out=args.assembly,args.binary,args.source,args.out;out.mkdir(parents=True,exist_ok=True)
assert source.name=='value_width_bench.cpp'
assert hashlib.sha256(source.read_bytes()).hexdigest()=='ada5ec41ab01ffc6048ac974f73ea81f9965b16b58c853a2489b601444905c75','requires actual frozen selector-mix-v1 driver'
commands=json.loads(args.compile_commands.read_text())
marker=f'CMakeFiles/hashtable_design_selector_mix_{args.key_bits}_{args.value_bytes}_{kind}_{args.selector_mix_variant}.dir/'
def command_tokens(entry):
 return entry['arguments'] if 'arguments' in entry else shlex.split(entry['command'])
matched=[r for r in commands if any(marker in t for t in command_tokens(r)) and Path(r['file']).name==source.name]
assert len(matched)==1,('exact selector-mix compile receipt missing or ambiguous',len(matched))
entry=matched[0];tokens=command_tokens(entry);command=shlex.join(tokens)
assert Path(tokens[tokens.index('-c')+1]).name==source.name
assert not any(t.startswith('-DHT_BAG_POLICIES') for t in tokens)
def macro_value(name,defaults_source=source):
 assert '-D'+name not in tokens,(name,'bare -D defines a nonzero control')
 explicit=[int(t.split('=',1)[1]) for t in tokens if t.startswith('-D'+name+'=')]
 if explicit:assert len(explicit)==1;return explicit[0]
 defaults=re.findall(r'^#define\s+'+re.escape(name)+r'\s+(\d+)\s*$',defaults_source.read_text(),re.M)
 assert len(defaults)==1,(name,'missing explicit or source-default macro')
 return int(defaults[0])
controls={'HT_VALUE_BYTES':args.value_bytes,'HT_VALUE_KEY':args.key_bits,
          'HT_GEOMETRY_POLICIES':1,'HT_OCCUPANCY_STUDY':1,'HT_GEOMETRY_KIND':kind,
          'HT_SELECTOR_HASH':int(args.selector_mix_variant=='multiply'),
          'HT_BUCKET_SENTINEL':0,'HT_RECIPE_SCAN32':1,'HT_HASH_MASK':2}
for name,expected in controls.items():
 assert any(t.startswith('-D'+name+'=') for t in tokens),(name,'selector-mix target must bind control explicitly')
 assert macro_value(name)==expected,(args.selector_mix_variant,name,'compile control mismatch')
assert macro_value('HT_VALUE_DIRECT')==0,'selector-mix requires composed selectors'
record_header=source.with_name('record_table.h')
record_header_sha256=hashlib.sha256(record_header.read_bytes()).hexdigest()
assert record_header_sha256=='d42bef76e4db55b6ed4a4bf3c8aeee827b336216c69a353a70805de1146f5165','requires captured selector-mix-v1 record_table.h'
control_bindings={}
for name,defaults_source in [('HT_VALUE_ROWS',source),('HT_RECORD_CONSUME',record_header)]:
 value=macro_value(name,defaults_source)
 assert value==0,(name,'row/immediate-consumer build is outside isolated selector-mix comparison')
 controls[name]=value
 explicit=any(t.startswith('-D'+name+'=') for t in tokens)
 control_bindings[name]=dict(value=value,origin='compile_definition' if explicit else 'captured_source_default',
  default_source=defaults_source.name,default_source_sha256=hashlib.sha256(defaults_source.read_bytes()).hexdigest())
# ReferenceA and each arm's actual bucket exponent remain separate.
geometry_source=re.search(r'constexpr\s+std::array\s+geometry_shapes\s*\{(.*?)\};',source.read_text(),re.S)
assert geometry_source,'missing frozen geometry definitions'
geometries=[tuple(map(int,x)) for x in re.findall(r'geometry_shape\{\s*(-?\d+)\s*,\s*(\d+)\s*\}',geometry_source[1])]
assert len(geometries)==16 and set(geometries)=={(shift,unit) for shift in [-2,-1,0,1] for unit in [64,96,128,192]}
assert re.search(r'emit\(\s*Kind\s*==\s*container_kind::set\s*\?\s*"ordered"\s*:\s*"serial"\s*,\s*serial\s*,',source.read_text()),'set ordered/map serial label changed'

assert '-fno-vectorize' in tokens and '-fno-slp-vectorize' in tokens
phase_lines={}
for line,text in enumerate(source.read_text().splitlines(),1):
 for m in re.finditer(r'(?:const )?auto\s+(\w+)\s*=\s*timed\b',text):phase_lines[line]=m[1]
base_phases={'construct','build','hit','miss','serial','update','erase','release'}
occupancy_phases={'aged_hit','aged_miss','reuse','reused_hit'}
assert base_phases<=set(phase_lines.values())<=base_phases|occupancy_phases
assert occupancy_phases<=set(phase_lines.values())
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
 if len(unique)==1:
  phase=next(iter(unique))
  return 'ordered' if kind==0 and phase=='serial' else phase
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
  root=dict(K=key[0],value_bytes=args.value_bytes,kind=key[1],reference_A=key[2],A=key[2]+(geometries[key[3]][0] if key[3]<16 else 0),arm=key[3],phase=phase,source_phase='serial' if phase=='ordered' else phase,symbol=symbol,clock=hex(clock),end=[hex(x) for x in ends],instructions=len(nodes),calls=cs,unknown_branches=unknown,phase_source_frames=locations[wrapper_call if wrapper_call else clock])
  if wrapper_call:root['wrapper_callsite']=hex(wrapper_call)
  roots.append(root)
  for c in cs:
   dest=target(c['target'])
   capture_callee(dest)
   if phase=='update' and dest in funcs:update_pending.append((dest,0))
  if key[1] in args.witness_kinds:
   prefix=f'K{key[0]}-W{args.value_bytes}-kind{key[1]}-RefA{key[2]}'
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
expected={(args.key_bits,k,a,arm,phase) for k in [kind] for a in [6,10] for arm in args.arms for phase in args.phases}
actual={(r['K'],r['kind'],r['reference_A'],r['arm'],r['phase']) for r in roots}
if actual!=expected:fail.append(dict(reason='root coverage differs',missing=sorted(expected-actual),extra=sorted(actual-expected)))
audit=dict(panel='selector_mix',compile_commands_sha256=hashlib.sha256(args.compile_commands.read_bytes()).hexdigest(),expected_regions=252,expected_read_regions=216,expected_update_regions=36,geometries=[dict(arm=i,bucket_shift=shape[0],unit_bytes=shape[1]) for i,shape in enumerate(geometries)],phase_semantics='ordered set traversal; not strict serial latency' if kind==0 else 'driver value-dependent serial; this audit qualifies call boundaries only',update_callgraph_truncated=truncated,value_bytes=args.value_bytes,key_bits=args.key_bits,compile_command=command,assembly_sha256=hashlib.sha256(assembly.read_bytes()).hexdigest(),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),phase_source_lines=phase_lines,timed_roots=roots,direct_callees=list(callees.values()),failures=fail)
audit.update(selector_mix_variant=args.selector_mix_variant,selector_mix_controls=controls,selector_mix_control_bindings=control_bindings,record_header_sha256=record_header_sha256,selector_mix_source_version='v1',container_kind=kind)
(out/'codegen.json').write_text(json.dumps(audit,indent=2)+'\n')
print('Source/DWARF resolved',len(roots),'regions;',sum('wrapper_callsite' in r for r in roots),'outlined timing wrappers;',len(fail),'failures')
print('First failures:',fail[:8])
if fail:sys.exit(1)
