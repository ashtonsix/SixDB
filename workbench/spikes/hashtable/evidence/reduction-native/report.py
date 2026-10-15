#!/usr/bin/env python3
"""Paired caller-reduction comparison; complete owners, unchanged-control phases."""
import argparse,collections,csv,json,math,statistics
from pathlib import Path
ARMS={0:'exact',1:'exact_direct',8:'abseil',9:'abseil_exact'}
CONTENDERS=('borrowed','local')
PHASES={'construct','build','hit','miss','update','erase','release','lifecycle'}

def summarize(path,repetitions=9):
    groups=collections.defaultdict(list);individual={}
    for raw in csv.DictReader(path.open()):
        row={k:v if k in ('contender','phase') else float(v) if k=='ns' else int(v) for k,v in raw.items()}
        assert row['contender'] in CONTENDERS and row['arm'] in ARMS,row
        assert all(math.isfinite(v) and v>=0 for k,v in row.items() if k not in ('contender','phase'))
        assert row['operations']>0
        key=tuple(row[k] for k in ('contender','arm','K','kind','A','n','phase'))
        groups[key].append(row)
        unique=(*key,row['rep']);assert unique not in individual,unique;individual[unique]=row
    cases={(r[2],r[3],r[4],r[5]) for r in groups}
    assert {(k,c,a) for k,c,a,n in cases}=={(k,c,a) for k in range(8,65,8) for c in range(3) for a in ((6,) if k==8 else (6,10))}
    expected={(v,arm,k,c,a,n,p) for k,c,a,n in cases for v in CONTENDERS for arm in ARMS for p in PHASES|{'serial_caller_cycle' if c==0 else 'serial_value_cycle'}}
    assert groups.keys()==expected,('missing/extra cells',expected-groups.keys(),groups.keys()-expected)
    out=[]
    for key,rows in sorted(groups.items()):
        assert sorted(r['rep'] for r in rows)==list(range(repetitions)),key
        assert len({r['operations'] for r in rows})==1,key
        for row in rows:
            if row['arm']<8:assert row['build_primary_rows']+row['build_secondary_rows']+row['build_overflow_rows']==row['n']
            if row['contender']=='local':
                other=individual[('borrowed',*key[1:],row['rep'])]
                # Physical backing is retained too, but equality is required only
                # for logical storage, geometry/population and operation counts.
                for field in ('operations','requested','metadata','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows'):
                    assert row[field]==other[field],(key,row['rep'],field,row[field],other[field])
        ns=[r['ns']/r['operations'] for r in rows]
        result=dict(zip(('contender','arm','K','kind','A','n','phase'),key));result.update(name=ARMS[key[1]],repetitions=repetitions,ns_per_operation=statistics.median(ns),minimum=min(ns),maximum=max(ns))
        result.update({k:statistics.median(r[k] for r in rows) for k in rows[0] if k not in {'contender','arm','K','kind','A','n','phase','rep','ns'}})
        out.append(result)
    lookup={tuple(r[k] for k in ('contender','arm','K','kind','A','n','phase')):r for r in out}
    for r in out:
        rest=tuple(r[k] for k in ('K','kind','A','n','phase'))
        for arm in ARMS:
            denominator=lookup[(r['contender'],arm,*rest)]['ns_per_operation']
            if denominator:r['ratio_to_'+ARMS[arm]]=r['ns_per_operation']/denominator
        denominator=lookup[('borrowed',r['arm'],*rest)]['ns_per_operation']
        if denominator:r['ratio_to_borrowed']=r['ns_per_operation']/denominator
    return out

if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__);p.add_argument('csv',type=Path);p.add_argument('--repetitions',type=int,default=9);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    results=summarize(args.csv,args.repetitions);args.output.write_text(json.dumps(results,indent=2)+'\n')
    print(f'Validated {len(results)} cells; all caller forms, selected arms and repetitions present. Per-repetition logical bytes/population controls match.')
    for r in results:
        if r['K']==40 and r['A']==10 and r['phase'] in ('hit','miss','serial_value_cycle','update','lifecycle'):
            print(f"{r['contender']:8} kind={r['kind']} {r['name']:12} {r['phase']:18} {r['ns_per_operation']:.3f} ns/op; ordinary Abseil ratio {r.get('ratio_to_abseil',0):.3f}; borrowed ratio {r.get('ratio_to_borrowed',0):.3f}")
