#!/usr/bin/env python3
"""Keep merge throughput, complete lifetime and sampled storage distinct."""
import argparse,csv,hashlib,json,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('root',type=Path);p.add_argument('--output',required=True,type=Path)
p.add_argument('--pattern',default='hashtable_design_consolidation_*-order*.csv')
a=p.parse_args();files=sorted(a.root.glob(a.pattern));assert files
fields=['K','kind','W','arm','source_keys','overlap','pattern','grain','patches','merge_mode']
metrics=['histories','input_values','build_ns','merge_ns','scan_ns','release_ns',
         'lifetime_ns','merge_ns_per_input','logical','backing','coexist_logical']
groups={};hashes={}
for path in files:
    rows=list(csv.DictReader(path.open()));assert rows
    has_modes='merge_mode' in rows[0]
    for r in rows:r.setdefault('patches','0');r.setdefault('merge_mode','fresh')
    assert len({r['patches'] for r in rows})==1 and rows[0]['patches'] in ['0','1']
    hashes[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
    identity={(r['K'],r['kind'],r['W']) for r in rows};assert len(identity)==1
    reps={int(r['rep']) for r in rows};assert reps==set(range(len(reps)))
    k,kind,w=next(iter(identity))
    arms={r['arm'] for r in rows}
    original={'scalar_groups','grouped','abseil','abseil_exact'}
    assert arms in [original,original|{'grouped_same_seed','encoded_same_seed'}]
    modes={'fresh','keep_left'} if has_modes else {'fresh'}
    assert {r['merge_mode'] for r in rows}==modes
    expected={(str(n),str(o),str(f),str(g),mode,str(rep),arm)
              for n in ([128] if k=='8' else [128,1024]) for o in range(2)
              for f in range(3 if kind=='2' else 1) for g in [16,64]
              for mode in modes for rep in reps for arm in arms}
    seen=set();digests={};mode_digests={}
    for r in rows:
        cell=tuple(r[f] for f in ['source_keys','overlap','pattern','grain','merge_mode','rep','arm'])
        assert cell not in seen;seen.add(cell)
        assert abs(sum(float(r[f]) for f in ['build_ns','merge_ns','scan_ns','release_ns'])-float(r['lifetime_ns']))<.01
        assert abs(float(r['merge_ns'])/int(r['input_values'])-float(r['merge_ns_per_input']))<.001
        if r['arm'].startswith('abseil'):assert float(r['backing'])==-1
        assert float(r['coexist_logical'])>=float(r['logical'])
        digests.setdefault(cell[:-1],set()).add((r['histories'],r['digest']))
        mode_digests.setdefault(tuple(r[f] for f in ['source_keys','overlap','pattern','grain','rep']),set()).add((r['histories'],r['digest']))
        groups.setdefault(tuple(r[f] for f in fields),[]).append(r)
    assert seen==expected,(path,seen^expected)
    assert all(len(d)==1 for d in digests.values()),'contenders disagree on complete output digest'
    assert all(len(d)==1 for d in mode_digests.values()),'merge modes disagree on complete output digest'
out=[]
for key,rows in sorted(groups.items()):
    row=dict(zip(fields,key));row['samples']=len(rows)
    for f in metrics:
        values=[float(r[f]) for r in rows]
        row[f]=statistics.median(values);row[f+'_min']=min(values);row[f+'_max']=max(values)
    out.append(row)
a.output.mkdir(parents=True,exist_ok=True)
with (a.output/'summary.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=list(out[0]));writer.writeheader();writer.writerows(out)
(a.output/'inputs.json').write_text(json.dumps(hashes,indent=2)+'\n')
print(f'{len(files)} complete processes, {sum(len(v) for v in groups.values())} rows, {len(out)} cases; no serial-latency or peak-RSS claim')
