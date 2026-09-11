from pathlib import Path
import csv, hashlib, json, math, re, shlex, statistics, subprocess, sys, tarfile
ROOT = Path('/home/ashtonsix/sixdb')
CAPTURE = ROOT/'build/workspaces/seriespack-bec-metadata-v2-20260911'
JOB = CAPTURE/'build/workers/20260911T042644Z-8d76720a'
OUT = JOB/'results/bec-metadata'
REVIEW = ROOT/'build/successor-bec-metadata/review'
REVIEW.mkdir(exist_ok=True)
read = lambda p: json.loads(p.read_text())
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
def require(v, why):
    if not v: raise ValueError(why)
def save(p, x): p.write_text(json.dumps(x, indent=2, sort_keys=True)+'\n')
receipt = read(OUT/'run.json')
capture = read(CAPTURE.with_name(CAPTURE.name+'.capture.json'))
manifest = read(JOB/'source-manifest.json')
require(receipt['status']=='complete' and receipt['source_unchanged'], 'run completion')
require(receipt['source_files_sha256']==capture['source_files_sha256']==manifest, 'captured source identities')
require(all(c['returncode']==0 for c in receipt['commands']), 'command status')
require(all(sha(OUT/n)==h for n,h in receipt['artifact_sha256'].items()), 'run artifact hashes')
for archive in [JOB/'source.tar.gz', OUT/'source.tar.gz']:
    with tarfile.open(archive) as t:
        files={m.name:hashlib.sha256(t.extractfile(m).read()).hexdigest() for m in t.getmembers() if m.isfile()}
    require(files==manifest, 'source archive:'+str(archive))
compiled=read(OUT/'compiled.json')
build=Path(next(c for c in receipt['commands'] if c['name']=='configure')['argv'][4])
source=Path(receipt['source_root'])
objects={}
for row in compiled:
    p=Path(row['file']); argv=shlex.split(row['command']); obj=Path(row['directory'])/argv[argv.index('-o')+1]
    relative=obj.relative_to(build).as_posix(); objects[relative]=row['object_sha256']
    if p.is_relative_to(source):
        name=p.relative_to(source).as_posix()
        require(row['source_sha256']==manifest[name], 'compiled repository source:'+name)
        for flag in ['-O3', '-g', '-march=x86-64-v4', '-mavx512vbmi', '-mavx512vbmi2', '-mgfni', '-mavx512vpopcntdq', '-mavx512bitalg', '-std=c++23']:
            require(flag in argv, 'compile flag:'+name+':'+flag)
        require('-flto' not in row['command'], 'unexpected LTO')
    else:
        tail=p.relative_to(build/'_deps/googlebenchmark-src')
        local=ROOT/'build/successor-bec-metadata/local/_deps/googlebenchmark-src'/tail
        require(sha(local)==row['source_sha256'], 'external compiled source:'+str(tail))
linked=read(OUT/'link-inputs.json')
members=0
for name,h in linked.items():
    p=OUT/'link-inputs'/name
    require(sha(p)==h, 'linked input hash:'+name)
    if p.suffix=='.o': require(objects[name]==h, 'linked direct object')
    else:
        target=p.stem.removeprefix('lib')
        for member in subprocess.check_output(['llvm-ar-21','t',p],text=True).splitlines():
            wanted=Path(name).parent/'CMakeFiles'/(target+'.dir')/member
            candidates=[value for key,value in objects.items() if key.startswith(str(Path(name).parent/'CMakeFiles'/(target+'.dir'))+'/') and Path(key).name==member]
            actual=hashlib.sha256(subprocess.check_output(['llvm-ar-21','p',p,member])).hexdigest()
            require(actual in candidates, 'archive member:'+name+':'+member)
            members+=1

MASK=(1<<64)-1
def mix(x):
    x=(x+0x9e3779b97f4a7c15)&MASK
    x=((x^(x>>30))*0xbf58476d1ce4e5b9)&MASK
    x=((x^(x>>27))*0x94d049bb133111eb)&MASK
    return x^(x>>31)
data=ROOT/'build/datasets/prepared/536c2f7c742270d974839b658fa20ef0c88342666d8dac5b7dc3021f6fddeaef'
prepared=read(data/'prepared.json')
require(prepared==read(OUT/'prepared.json'), 'same prepared data')
require(all(sha(data/n)==h for n,h in prepared['files_sha256'].items()), 'dataset files')
workloads={}
for name in ['structural', 'random_half', 'structural_tail129']:
    n=129 if name=='structural_tail129' else 256
    windows=[]
    for w in range(8):
        raw=bytearray(n*32); seed=w*17
        for i in range(n):
            if name=='random_half':
                for b in range(32): raw[i*32+b]=mix(w*8192+i*32+b+811)&255
            else:
                for j in range((i+seed)%257):
                    pos=(j*157+i*11+seed)%256
                    raw[i*32+pos//8]|=1<<(pos%8)
        windows.append(bytes(raw))
    workloads[name]=windows
for p in sorted(data.glob('*.windows')):
    raw=p.read_bytes(); workloads[p.stem]=[raw[i:i+8192] for i in range(0,len(raw),8192)]
hashes={}; counts={}
for name,windows in workloads.items():
    checksum=0; n=len(windows[0])//32
    for raw in windows:
        for b in raw: checksum=((checksum^b)*1099511628211)&MASK
    hashes[name]=checksum
    for first,count in [(0,n),(3,37),(15,18),(15,2),(127,2),(n-1,1)]:
        total=0
        for w,raw in enumerate(windows):
            total+=sum((raw[i]&(mix(1234+w*8192+i)&255)).bit_count() for i in range(first*32,(first+count)*32))
        counts[name,first,count]=total
samples=list(csv.DictReader((OUT/'samples.csv').open()))
require(len(samples)==4050, 'sample count')
for row in samples:
    name,cut,*args=row['case'].split('/')
    h=int(float(row['input_hash_lo'])) | int(float(row['input_hash_hi']))<<32
    require(h==hashes[name], 'independent input hash:'+name)
    if cut!='refill': require(float(row['expected_count_sum'])==counts[name,*map(int,args)], 'independent query result')
    require(float(row['bound_target'])=={'avx2':2,'avx512':3}[receipt['materialized_target']], 'chosen control target')
screen=read(OUT/'target-screen.json'); observations={}
for b,target in enumerate(['avx2','avx512','avx512','avx2']):
    raw=read(OUT/f'screen-{b}-{target}.json')
    rows=[r for r in raw['benchmarks'] if r['run_type']=='iteration']
    expected=[]
    for phase in [2,3]:
        for name in ['structural','random_half','structural_tail129']:
            for g in ([0,64,128] if name=='structural_tail129' else [0,128,240]):
                expected.extend([f'bec/{phase}-materialized/{name}/refill/g{g}']*3)
    require([r['name'] for r in rows]==expected, 'screen order')
    for index,r in enumerate(rows):
        require(not r.get('error_occurred') and r['time_unit']=='ns' and r['repetition_index']==index%3 and r['threads']==1, 'screen status')
        require(r['bound_target']=={'avx2':2,'avx512':3}[target], 'screen target')
        require(math.isclose(1e9/r['items_per_second'],r['cpu_time']/256,rel_tol=1e-9), 'screen unit scale')
        case=r['name'].split('/',2)[2]
        observations.setdefault(case,{}).setdefault(target,[]).append(r['cpu_time']/256)
require(observations==screen['samples'], 'screen repetitions')
ratios={k:statistics.median(v['avx2'])/statistics.median(v['avx512']) for k,v in observations.items()}
require(ratios==screen['avx2_over_avx512'], 'screen ratios')
chosen='avx2' if statistics.median(ratios.values())<1 else 'avx512'
require(chosen==screen['chosen']==receipt['materialized_target'], 'control selection')

archive=OUT/'link-inputs/workbench/spikes/ikea-composition/validation/bec-metadata/libikea_bec_metadata_adapter.a'
symbols=[]
for line in subprocess.check_output(['llvm-nm-21','-S','-C',archive],text=True).splitlines():
    parts=line.split(maxsplit=3)
    if len(parts)!=4: continue
    if re.search(r'bec_metadata::(count<|refill_stored<)',parts[3]):
        symbols.append(dict(name=parts[3],bytes=int(parts[1],16)))
require(len(symbols)==9, 'nine adapter endpoints')
asm=subprocess.check_output(['llvm-objdump-21','-dr','-C','--no-show-raw-insn',archive],text=True)
for row in symbols:
    block=next(b for b in re.split(r'(?m)^(?=[0-9a-f]+ <)',asm) if b.startswith('0000000000000000 <'+row['name']+'>:'))
    ins=[line for line in block.splitlines()[1:] if re.match(r'^\s+[0-9a-f]+:',line) and 'R_X86_64_' not in line]
    row.update(instructions=len(ins), calls=sum(bool(re.search(r'\bcallq?\b',i)) for i in ins),
        vector_stack=sum('(%rsp' in i and bool(re.search(r'[xyz]mm\d+',i)) for i in ins))
    r=re.search(r'\(bec_metadata::Reader\)(\d)',row['name'])[1]
    e=re.search(r'\(ikea::heterogeneous::Execution\)(\d)',row['name'])
    name=('count-'+e[1] if e else 'refill')+'-'+['specialized','native','materialized'][int(r)]+'.asm'
    if e is None: (REVIEW/name).write_text(block)
save(REVIEW/'codegen.json',dict(archive_sha256=sha(archive),symbols=symbols,
    source_lines={p.name:len(p.read_text().splitlines()) for p in (CAPTURE/'workbench/spikes/ikea-composition/validation/bec-metadata').glob('*') if p.suffix in ['.h','.cpp']}))
save(REVIEW/'audit.json',dict(status='passed',job=JOB.name,capture_digest=capture['source_digest'],
    source_files=len(manifest),compiled_sources=len(compiled),archive_members=members,linked_inputs=len(linked),
    timing_samples=4050,preflights=1350,screen_samples=216,independent_input_hashes=hashes,
    independent_count_contexts=len(counts),source_and_artifact_hashes_verified=True,
    external_googlebenchmark_sources_matched_local_pinned_copy=True,
    local_release_check=(ROOT/'build/successor-bec-metadata/local-check.txt').read_text(),
    local_sanitizer_check=(ROOT/'build/successor-bec-metadata/sanitize-check.txt').read_text()))
print(json.dumps(read(REVIEW/'audit.json'),indent=2))
print(json.dumps(symbols,indent=2))
