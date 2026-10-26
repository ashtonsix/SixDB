import hashlib,json,shlex,subprocess
from pathlib import Path
root=Path('/home/ashtonsix/sixdb')
build=root/'build/clang/frame3rt'
out=root/'build/frame-collab/phase3/extraction-sanitize'
out.mkdir(exist_ok=True)
commands=json.loads((build/'compile_commands.json').read_text())
flags=['-O1','-gline-tables-only','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
def compile_unit(entry,target):
    args=shlex.split(entry['command'])
    args=[a for a in args if a not in ('-O3','-g')]
    args[args.index('-o')+1]=str(target)
    subprocess.run(args+flags,cwd=build,check=True)
objects=[]
sources=[]
for entry in commands:
    source=Path(entry['file'])
    if '/ikea/src/frame/' not in str(source): continue
    obj=out/('_'.join(source.relative_to(root/'ikea/src/frame').parts)+'.o')
    compile_unit(entry,obj)
    objects.append(obj)
    sources.append(source)
libraries=[build/'ikea'/name for name in ('libikea_seriespack.a','libikea_core.a','libikea_intervals.a')]
checks=[f'test/frame/{name}.cpp' for name in ('description','manifest','group_image','reader','stages')]+['test/intervals/wide.cpp']
for check in checks:
    entry=next(x for x in commands if x['file'].endswith('/ikea/'+check))
    target=out/(check.replace('/','_').replace('.cpp',''))
    obj=target.with_suffix('.o')
    compile_unit(entry,obj)
    subprocess.run(['/usr/bin/clang++-21',*flags,'-Wl,--gc-sections',str(obj),*[str(x) for x in objects],*[str(x) for x in libraries],'-o',str(target)],check=True)
    subprocess.run([str(target)],check=True)
    sources.append(Path(entry['file']))
receipt={'scope':'Frame implementation and six fixtures ASan+UBSan; linked existing non-sanitized SeriesPack/core/Intervals archives','flags':flags,'sources':{str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in sources},'archives':{str(x.relative_to(root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in libraries}}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('PASS: all six extraction sanitizer fixtures')
