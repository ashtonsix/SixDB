#!/usr/bin/env python3
"""Keep complete-operation serial, reciprocal and storage measurements separate."""
import argparse,csv,json,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path)
p.add_argument('--graphs',nargs='+',type=int,choices=(0,1),default=[0,1])
p.add_argument('--concurrencies',nargs='+',type=int,choices=(1,32),default=[1,32]);a=p.parse_args()
summary=[]
for path in sorted(a.root.glob('hashtable_design_staged_*.csv')):
    if path.name.endswith('-check.csv'):continue
    rows=list(csv.DictReader(path.open()));assert rows,path
    graph_hints={int(r.get('graph_hints',0)) for r in rows};assert len(graph_hints)==1
    graph_hint=next(iter(graph_hints));assert graph_hint in (0,1)
    k,w,kind,n=(int(rows[0][x]) for x in ('K','W','kind','keys'))
    word_control=int(rows[0].get('word_control',int(kind!=2 and w<=8)))
    owners={'abseil','generic'}|({'word'} if word_control else set())
    groups={}
    for r in rows:
        assert all(int(r[x])==v for x,v in zip(('K','W','kind','keys'),(k,w,kind,n)))
        assert r['owner'] in owners and int(r['concurrency']) in (1,32) and int(r['mixed']) in (0,1)
        assert int(r['requests'])%(n*(1+int(r['mixed'])))==0
        assert int(r['requested'])>0 and int(r['metadata'])>0 and float(r['ns_per_request'])>0
        key=tuple(r[x] for x in ('mixed','concurrency','owner','method'));groups.setdefault(key,[]).append(r)
    expected={(str(m),str(c),o,method) for m in a.graphs for c in a.concurrencies for o in owners
              for method in (('direct','prefetch') if o=='abseil' else ('direct','manual','manual_hints','ring','ring_hints'))}
    assert set(groups)==expected,(path,set(groups)^expected)
    reps={int(r['rep']) for r in rows};assert reps==set(range(len(reps)))
    for key,batch in sorted(groups.items()):
        assert len(batch)==len(reps) and {int(r['rep']) for r in batch}==reps
        x={f:batch[0][f] for f in ('K','W','kind','A','B','unit','keys','mixed','concurrency','owner','method')}
        x['run']=path.stem;x['samples']=len(batch)
        x['graph_hints']=graph_hint
        for field in ('secondary_unit','capacity_family','read_windows','word_control','inline_bytes','primary_slots','secondary_slots','hint_bytes_per_graph','hint_windows_per_graph'):
            if field in batch[0]:
                assert len({r[field] for r in batch})==1,(path,key,field)
                x[field]=int(batch[0][field])
        for field in ('requested','backing','metadata','jobs_bytes','graph_bytes','ring_bytes','requests','ring_visits'):
            assert len({r[field] for r in batch})==1,(path,key,field)
            x[field]=int(batch[0][field])
        x['logical_bytes']=x['requested']+x['metadata']
        times=[float(r['ns_per_request']) for r in batch]
        x['ns_per_request']=statistics.median(times);x['min_ns']=min(times);x['max_ns']=max(times)
        summary.append(x)
assert summary,'no timing CSVs'
with (a.root/'summary.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=list(summary[0]));writer.writeheader();writer.writerows(summary)
(a.root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
for x in summary:
    if x['method'] in ('direct','ring_hints','prefetch'):
        print(f"{x['run']} mixed={x['mixed']} C{x['concurrency']} {x['owner']}/{x['method']}: {x['ns_per_request']:.3f} ns; logical={x['logical_bytes']} backing={x['backing'] if x['owner']!='abseil' else 'unmeasured'} bytes")
