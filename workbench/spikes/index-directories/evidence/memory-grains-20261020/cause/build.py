import hashlib,json,pathlib,subprocess
root=pathlib.Path('/home/ashtonsix/sixdb')
out=root/'build/reviews/loom-grain-cause-20261020'
arm=root/'build/reviews/loom-index-headerfree-20261020/arm'
base=root/'build/architecture-index-memory-fix-20261020/grains'
commands=json.loads((arm/'commands.json').read_text())
inputs=json.loads((base/'read-inputs.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def local(p):
    prefix='/Users/ashtonsix/OrbStack/ubuntu/home/ashtonsix/sixdb/'
    return root/p[len(prefix):] if p.startswith(prefix) else pathlib.Path(p)
assert all(sha(local(p))==h for p,h in inputs.items())
compile_fixture=next(c['argv'] for c in commands if c['label']=='compile-group_fixture').copy()
compile_fixture[compile_fixture.index('-c')+1]=str(out/'fixture.cpp')
compile_fixture[compile_fixture.index('-o')+1]=str(out/'fixture.o')
compile_cause=next(c['argv'] for c in json.loads((base/'read-build.json').read_text()) if c['label']=='compile-read').copy()
compile_cause[compile_cause.index('-c')+1]=str(out/'cause.cpp')
compile_cause[compile_cause.index('-o')+1]=str(out/'cause.o')
link=json.loads((base/'read-build.json').read_text())[-1]['argv'].copy()
link[link.index(str(arm/'common/group_fixture.o'))]=str(out/'fixture.o')
link[link.index(str(base/'screen.o'))]=str(out/'cause.o')
link[link.index('-o')+1]=str(out/'cause')
log=[]
for label,argv in [('compile-fixture',compile_fixture),('compile-cause',compile_cause),('link-cause',link)]:
    p=subprocess.run(argv,cwd=root,capture_output=True,text=True)
    log.append(dict(label=label,argv=argv,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
    (out/'commands.json').write_text(json.dumps(log,indent=2)+'\n')
    if p.returncode:raise SystemExit(p.stderr[:6000])
for p in [out/'fixture.cpp',out/'fixture.o',out/'cause.cpp',out/'cause.o',out/'cause',pathlib.Path(__file__)]:inputs[str(p)]=sha(p)
(out/'inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
print(json.dumps(dict(binary_sha256=sha(out/'cause'),inputs=len(inputs),timing_collected=False)))
