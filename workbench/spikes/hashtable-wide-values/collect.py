#!/usr/bin/env python3
"""Verify original case receipts; keep complete repetitions in compact CSVs.

Old prefix-only manual prefetch is excluded in favor of its paired complete-
window follow-up. All implementations, negative results, seeds and repetitions
remain for the retained operation families. Original bundles retain every argv,
source, binary and assembly file, including cases omitted from this export.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil

p = argparse.ArgumentParser()
p.add_argument('recovered', type=Path)
p.add_argument('density', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--staged', type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
packed = {'materialized', 'borrow', 'slots', 'slots32'}
recovery=json.loads((a.recovered/'resumed/recovery.json').read_text())
origins = set(recovery['verified_complete_cases'])
jobs={'main':json.loads((a.recovered/'job.json').read_text())['id'],
      'window':json.loads((a.recovered/'job.json').read_text())['id'],
      'density':json.loads((a.density/'job.json').read_text())['id']}
if a.staged: jobs['staged']=json.loads((a.staged/'job.json').read_text())['id']
original_job=json.loads((a.recovered/'resumed/job.json').read_text())['id']
manifest = []
cohorts=[('main', a.recovered/'resumed'), ('window', a.recovered/'complete-window'), ('density', a.density)]
if a.staged: cohorts.append(('staged',a.staged/'complete-window'))
for cohort, root in cohorts:
    rows, memory = [], set()
    plan = json.loads((root/'plan.json').read_text())
    for case in plan:
        path = root/'samples'/(case['case']+'.csv')
        receipt = json.loads(path.with_suffix('.json').read_text())
        sha = hashlib.sha256(path.read_bytes()).hexdigest()
        assert receipt['returncode'] == 0 and receipt['sha256'] == sha, path
        assert receipt['case'] == case['case']
        assert receipt['argv'][receipt['argv'].index('--impl'):] == case['argv'][case['argv'].index('--impl'):], path
        original = original_job if cohort == 'main' and case['case'] in origins else jobs[cohort]
        kept = 0
        for row in csv.DictReader(path.open()):
            row.setdefault('loom', '0')
            row.setdefault('staged', '0')
            if cohort == 'main':
                if row['pipeline'] != '0': continue
                if row['operation'] == 'read' and row['temperature'] == 'cold' and int(row['v']) >= 32: continue
                normal = row['hint'] == ('auto' if row['impl'] in packed and row['operation'] in ['read','memory'] else 'p')
                diagnosis = row['impl'] in ['materialized','borrow'] and row['temperature'] == 'warm' and row['w'] == '32' and ((row['v'] == '8' and row['population'] == '13926') or (row['v'] == '64' and row['population'] == '6963'))
                if not normal and not diagnosis: continue
            if cohort == 'window' and (row['w'] != '32' or row['v'] != '64') and row['impl'] != 'f14_slots': continue
            if cohort == 'window' and row['impl'] == 'f14_slots' and (row['w'] != '32' or row['v'] != '64') and row['operation'] not in ['replace','increment','insert','upsert50','erase','refill','growth','memory']: continue
            if cohort == 'density' and row['temperature'] == 'cold' and row['operation'] == 'read': continue
            if row['operation'] == 'memory':
                key = tuple(row[f] for f in ['impl','w','b','v','population','temperature','seed'])
                if key in memory: continue
                memory.add(key)
            rows.append({'cohort':cohort, 'job':original, 'case':case['case'], 'fixture_b':case['case'].split('-')[1], **row})
            kept += 1
        manifest.append({'cohort':cohort, 'case':case['case'], 'job':original, 'sha256':sha, 'retained_rows':kept})
    assert rows
    with (a.output/(cohort+'.csv')).open('w') as out:
        writer=csv.DictWriter(out,fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)
(a.output/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
contexts=[('original',a.recovered/'resumed'),('recovered',a.recovered),('density',a.density)]
if a.staged: contexts.append(('staged',a.staged))
for name, root in contexts:
    for f in ['host.json','source-manifest.json','worker-result.json','job.json']:
        shutil.copyfile(root/f,a.output/(name+'-'+f))
for src,dst in [(a.recovered/'complete-window/sanitizer/receipt.json','native-sanitizer.json'),
                (a.recovered/'resumed/completion.json','completion.json'),
                (a.recovered/'complete-window/lscpu.json','window-lscpu.json'),
                (a.recovered/'complete-window/compiler.txt','compiler.txt'),
                (a.recovered/'complete-window/packages.txt','packages.txt')]:
    shutil.copyfile(src,a.output/dst)
# These are recovery references to the unmodified original captures, not new
# measurements or a claim that resumed cases ran on the original Spot worker.
archives = {
 'main_original': {'job':'20261021T130039Z-b8b3dd4e','sha256':'dd117b74e334c5a2f5baa07f23e90051b86a3801580bfc3f1b5ff099ea0e6d7e'},
 'main_recovered_and_window': {'job':'20261021T132855Z-22670938','sha256':'6bc3d772b695006fd512ffd7657cbe1371e44120177516a512d869dcd9acbfef'},
 'density': {'job':'20261021T134601Z-97589074','sha256':json.loads((a.density.parent/'artifact.json').read_text())['sha256']} if (a.density.parent/'artifact.json').exists() else {'job':'20261021T134601Z-97589074'}
}
if a.staged:
    archives['staged']={'job':'20261021T135902Z-60f77ea3',**json.loads((a.staged.parent/'artifact.json').read_text())}
    shutil.copyfile(a.staged/'complete-window/sanitizer-staged/receipt.json',a.output/'native-staged-sanitizer.json')
(a.output/'original-archives.json').write_text(json.dumps(archives,indent=2)+'\n')
print(json.dumps({'cases_verified':len(manifest),'retained_rows':sum(x['retained_rows'] for x in manifest)},indent=2))
