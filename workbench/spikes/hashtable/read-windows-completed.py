#!/usr/bin/env python3
"""Recover complete matched mode groups without relabeling a timed-out job."""
import argparse,csv,hashlib,json,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
groups={}
for f in sorted(a.source.glob('hashtable_design_staged_windows_*.csv')):
    if f.name.endswith('-check.csv'):continue
    group,mode=f.stem.rsplit('_',1);groups.setdefault(group,{})[int(mode)]=f
selected={};excluded={}
for group,modes in groups.items():
    reasons=[]
    if set(modes)!={0,1,2}:reasons.append('missing read-window mode')
    for mode,f in modes.items():
        rows=list(csv.DictReader(f.open()));reps={r['rep'] for r in rows}
        resource=f.with_name(f.stem+'-resources.txt')
        if not rows or len(rows)!=28*len(reps):reasons.append(f'mode{mode}: incomplete operation matrix')
        if not resource.exists() or 'Exit status: 0' not in resource.read_text():reasons.append(f'mode{mode}: no successful process exit receipt')
    if reasons:excluded[group]=reasons;continue
    for f in modes.values():
        selected[f.name]=hashlib.sha256(f.read_bytes()).hexdigest();shutil.copy2(f,a.output/f.name)
        for suffix in ['-resources.txt','-codegen.json']:
            src=f.with_name(f.stem+suffix);shutil.copy2(src,a.output/src.name)
        # Full physical receipts remain recoverable in the parent archive;
        # preserve the additional >LLC wide-map cohort directly here too.
        if '_a21_' in f.name:
            source=a.source/('residence-'+f.stem)
            assert source.is_dir(),source
            shutil.copytree(source,a.output/source.name)
for name in ['hardware.json','lscpu.json','compiler.txt','boundaries.txt']:
    shutil.copy2(a.source/name,a.output/name)
shutil.copy2(a.source/'worker-result.json',a.output/'origin-result.json')
shutil.copy2(a.source.parent/'artifact.json',a.output/'parent.artifact.json')
(a.output/'completed.json').write_text(json.dumps({'parent_job':a.source.parent.name,'parent_outcome':json.loads((a.source/'worker-result.json').read_text()),'selected':selected,'excluded_matched_groups':excluded},indent=2)+'\n')
print(f'{len(selected)} successfully exited processes; {len(excluded)} incomplete matched groups excluded; parent outcome preserved')
