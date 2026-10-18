#!/usr/bin/env python3
"""Inspect bounded filter witnesses and model selected basic blocks (not timings)."""
import argparse,collections,hashlib,json,pathlib,re,shlex,subprocess
p=argparse.ArgumentParser();p.add_argument('build',type=pathlib.Path);p.add_argument('out',type=pathlib.Path);p.add_argument('profile');p.add_argument('cpu');a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=True)
commands=json.loads((a.build/'compile_commands.json').read_text())
c=next(c for c in commands if c['file'].endswith('/tile-codegen.cpp'))
args=shlex.split(c['command']); i=args.index('-o');del args[i:i+2];args.remove('-c')
asm=a.out/(a.profile+'-kernels.s');args+=['-S','-o',str(asm.resolve())]
subprocess.run(args,cwd=c['directory'],check=True)
obj=a.build/'workbench/spikes/filter-composition/CMakeFiles/filter_tile_codegen.dir/tile-codegen.cpp.o'
dis=subprocess.check_output(['llvm-objdump-21','-d','--no-show-raw-insn',str(obj)],text=True)
(a.out/(a.profile+'-kernels.txt')).write_text(dis)
bodies={};name=None
for line in dis.splitlines():
    match=re.match(r'^[0-9a-f]+ <(tile_[^>]+)>:',line)
    if match:name=match[1];bodies[name]=[]
    elif re.match(r'^[0-9a-f]+ <',line): name=None
    elif name:
        m=re.match(r'\s*[0-9a-f]+:\s+([a-z][a-z0-9.]*)\s*(.*)',line)
        if m:bodies[name].append((m[1],m[2]))
report={'scope':'Standalone runtime-input consumers; static instructions and LLVM scheduling estimates are not measured latency/throughput. Native whole-loop results are separate.',
        'asm_sha256':hashlib.sha256(asm.read_bytes()).hexdigest(),'bodies':{}}
for n,ops in bodies.items():
    report['bodies'][n]={'instructions':len(ops),'opcodes':dict(collections.Counter(o for o,_ in ops)),
        'calls':[(o,v) for o,v in ops if o in ('bl','blr','call','callq')],
        'stack':[(o,v) for o,v in ops if re.search(r'\[(?:sp|x29)(?:,|\])|\(%rsp\)',v)]}
assert len(bodies)==104,len(bodies)
assert not any(b['calls'] for b in report['bodies'].values())
text=asm.read_text().splitlines();selected={}
for n in ['tile_16_8_0_0','tile_16_8_0_2','tile_30_8_0_0','tile_30_8_0_2','tile_16_24_0_0','tile_16_24_0_2','tile_30_16_0_0','tile_30_16_0_2']:
    start=next(i for i,l in enumerate(text) if l.startswith(n+':'))
    end=next(i for i in range(start+1,len(text)) if text[i].startswith('.Lfunc_end'))
    body='\n'.join(l for l in text[start+1:end] if not l.strip().startswith(('.cfi','.size')))+'\n'
    result=subprocess.run(['llvm-mca-21','-mcpu='+a.cpu,'-iterations=100'],input=body,text=True,capture_output=True)
    (a.out/(a.profile+'-'+n+'-mca.txt')).write_text(result.stdout+result.stderr)
    selected[n]={'returncode':result.returncode,'summary':result.stdout.split('Instruction Info:')[0].strip()}
report['llvm_mca']=selected
(a.out/(a.profile+'-codegen.json')).write_text(json.dumps(report,indent=2)+'\n')
assert all(r['returncode']==0 for r in selected.values()),selected
print(f'{len(bodies)} bounded kernels: no helper calls; model output retained for 8 paired witnesses')
