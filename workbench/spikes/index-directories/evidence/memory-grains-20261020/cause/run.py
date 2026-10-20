import csv,hashlib,io,json,pathlib,statistics,subprocess,sys,time
root=pathlib.Path('/home/ashtonsix/sixdb');out=root/'build/reviews/loom-grain-cause-20261020'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def local(p):
    prefix='/Users/ashtonsix/OrbStack/ubuntu/home/ashtonsix/sixdb/'
    return root/p[len(prefix):] if p.startswith(prefix) else pathlib.Path(p)
phase=sys.argv[1];assert phase in ['qualify','time']
assert all(sha(local(p))==h for p,h in json.loads((out/'inputs.json').read_text()).items())
receipt=dict(kind='Same actual Segment addresses; endpoint-only causal diagnostic',phase=phase,
    source_digest='acad3cc8534a3c004aa3f2d139f56ff37342c6ea20464f3a690e0a8535b2c046',
    binary_before=sha(out/'cause'),started_unix=time.time(),runs=[],
    limit='Three full graphs/control stocks simultaneously live; diagnostic, not an isolated complete caller or peer comparison. No hardware PMU counters.')
for order in [0,1]:
    argv=['taskset','-c','8' if phase=='time' else '7',str(out/'cause'),'262144','16',str(order),'1048576' if phase=='time' else '65536',phase]
    p=subprocess.run(argv,cwd=root,capture_output=True,text=True)
    label=f'{phase}-o{order}';(out/(label+'.stdout')).write_text(p.stdout);(out/(label+'.stderr')).write_text(p.stderr)
    run=dict(argv=argv,returncode=p.returncode,stdout=label+'.stdout',stderr=label+'.stderr')
    receipt['runs'].append(run);(out/(phase+'-receipt.json')).write_text(json.dumps(receipt,indent=2)+'\n')
    if p.returncode:raise SystemExit(p.stderr[-5000:])
    records=[json.loads(line) for line in p.stdout.splitlines() if line.startswith('{')]
    associations=[r for r in records if r['kind']=='association']
    assert len(associations)==3 and all(r['checked']==262144 for r in associations)
    assert all(r['same_actual_material_addresses'] for r in associations[1:])
    materials=[r for r in records if r['kind']=='inventory' and r['bytes']=='material64']
    assert len({(r['min'],r['max'],r['normalized_fnv']) for r in materials})==1
    if phase=='time':
        csv_text=p.stdout[p.stdout.index('trial,position,operation,'):]
        rows=list(csv.DictReader(io.StringIO(csv_text)));assert len(rows)==18
        run['median_wall_ns']={a:statistics.median(float(r['wall_ns_op']) for r in rows if r['arm']==a) for a in ['original','shared_D16','shared_D256']}
receipt['binary_after']=sha(out/'cause');assert receipt['binary_before']==receipt['binary_after']
receipt['finished_unix']=time.time();(out/(phase+'-receipt.json')).write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
