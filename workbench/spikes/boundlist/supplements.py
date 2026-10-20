#!/usr/bin/env python3
"""Retain source-preparation accounting and complete linked-consumer audit counts."""
import csv,hashlib,json,pathlib,re,subprocess,sys
spec=json.loads(pathlib.Path(sys.argv[1]).read_text());out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
inputs=[];preparation=[];audits=[]
for host,job in spec['search']['jobs'].items():
    root=pathlib.Path('build/workers')/job
    assert json.loads((root/'status.json').read_text())['state']=='complete'
    for p in sorted((root/'results').glob('*-adoption-k*-preparation.txt')):
        match=re.fullmatch(r'(.*)-adoption-k(\d+)-n(\d+)-d(\d+)-preparation.txt',p.name)
        values=re.search(r'production_source_prepare_ns=([\d.]+) binding_bytes=(\d+)',p.read_text())
        assert match and values,p
        isa,bits,count,distribution=match.groups();ns,storage=values.groups()
        preparation.append([host,isa,bits,count,distribution,ns,storage,4096])
        inputs.append({'host':host,'job':job,'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    collection=json.loads((root/'collection.json').read_text())
    binary_names=sorted(n for n in collection['members'] if n.endswith('-binary'))
    assert len(binary_names)==(5 if host=='neon' else 10),(host,binary_names)
    missing=[name for name in binary_names if not (root/'results'/name).exists()]
    if missing:
        command=[sys.executable,'workbench/tools/worker.py','fetch',job]
        for name in missing:command.extend(['--file',name])
        subprocess.run(command,check=True)
    for name in binary_names:
        binary=root/'results'/name
        assert hashlib.sha256(binary.read_bytes()).hexdigest()==collection['members'][name]['sha256']
        isa,target=binary.name.removesuffix('-binary').split('-',1)
        asm=out/f'audit-{job}-{isa}-{target}.txt'
        subprocess.run([sys.executable,'workbench/spikes/boundlist/qualification-audit.py',str(binary),str(asm)],check=True)
        report=json.loads(asm.with_suffix('.json').read_text());functions=report['consumers']
        callbacks=sum(bool(re.match(r'(?:void )?native<.*>::(?:header|metadata|payload)\(',x['function'])) for x in functions)
        assert not any(x['calls'] for x in functions)
        audits.append([host,isa,target,len(functions),callbacks,0,hashlib.sha256(binary.read_bytes()).hexdigest()])
        inputs.append({'host':host,'job':job,'file':binary.name,'sha256':hashlib.sha256(binary.read_bytes()).hexdigest()})
        asm.unlink();asm.with_suffix('.json').unlink() # Raw linked dumps stay in the verified worker archive.
for name,fields,rows in [
    ('preparation',['host','isa','bits','count','distribution','hot_loop_mean_ns','binding_bytes','preparations_per_loop'],preparation),
    ('calls',['host','isa','target','consumer_functions','ring_callbacks','unexpected_calls','binary_sha256'],audits)]:
    with (out/f'{name}.csv').open('w') as f:
        w=csv.writer(f);w.writerow(fields);w.writerows(rows)
(out/'supplement-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
