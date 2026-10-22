#!/usr/bin/env python3
"""Bounded cross-column consumers over retained logical transports and controls."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import random
import subprocess
import statistics
import sys
import workload as w
from check import coarse_example
from encoded_check import kinds_example


def authored(correlated, rows=65536, parts=3):
    doc={'format':w.FORMAT,'kind':'authored-synthetic','snapshot':'basis-0',
         'source':{'law':'X=20000+257*latent_category; decorrelation permutes X within each part, preserving both marginals',
                   'seed':713,'nulls':'none','correlated':correlated},'parts':[],'queries':[]}
    dictionary=[713+10000000019*a for a in range(256)]
    for p in range(parts):
        rng=random.Random(713+p);cs=[rng.randrange(256) for _ in range(rows)]
        xs=[20000+257*c for c in cs]
        if not correlated:rng.shuffle(xs)
        keys=dictionary[p:]+dictionary[:p]
        doc['parts'].append({'part_id':f'p{p}','dictionary_edition':f'rotated-{p}',
            'position_limit':rows,'dictionary':keys,
            'rows':[{'position':i,'category':dictionary[c],'x':x} for i,(c,x) in enumerate(zip(cs,xs))]})
    all_rows={p['part_id']:None for p in doc['parts']}
    every_third={p['part_id']:list(range(0,rows,3)) for p in doc['parts']}
    doc['queries']=[w.query('all',all_rows),w.query('half-X',all_rows,min_x=52000),
                    w.query('scattered',every_third,min_x=1),
                    w.query('few-categories',all_rows,categories=dictionary[:2],include_null=False)]
    w.validate(doc);return doc


def evaluate(binary,doc,trials):
    command=['taskset','-c','6',str(binary),str(trials)]
    output=subprocess.run(command,input=w.transport(doc),text=True,capture_output=True,timeout=300)
    if output.returncode:raise RuntimeError(output.stderr+'\n'+output.stdout[-2000:])
    records=[json.loads(line) for line in output.stdout.splitlines()]
    assert records[-1]['type']=='checked'
    return records


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--trials',type=int,default=18);ap.add_argument('--checks-only',action='store_true')
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    checks=[w.example(),coarse_example(),w.generate(713,rows=17,parts=2),kinds_example()]
    checks += [w.mask_variants(w.example(),positions_per_block=127,blocks_per_group=5)]
    for i,doc in enumerate(checks):
        rows=evaluate(args.binary,doc,0)
        (args.output/f'check-{i}.json').write_text(json.dumps(rows,indent=2)+'\n')
    if args.checks_only:return
    sources=[('OS-ResolutionWidth',Path('build/spikes/ikea-frame/clickbench-os-resolution.json')),
             ('Region-ConnectTiming',Path('build/spikes/ikea-frame/clickbench-region-connect.json')),
             ('IMDb-year',Path('build/spikes/ikea-frame/imdb-title-year.json'))]
    docs=[(name,json.loads(path.read_text()),{'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}) for name,path in sources]
    docs += [(name,authored(correlated),{'generator':'authored()','correlated':correlated}) for name,correlated in [('functional',True),('decorrelated',False)]]
    summaries=[];identities=[]
    for name,doc,source in docs:
        # Preserve unabridged queries/selection recipes by source reference.
        if name in ('functional','decorrelated'):
            path=args.output/(name+'-logical.json');path.write_text(json.dumps(doc,separators=(',',':'))+'\n')
            source['logical_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
        result=evaluate(args.binary,doc,args.trials)
        (args.output/(name+'.json')).write_text(json.dumps(result,indent=2)+'\n')
        identities.append({'input':name,'source':source,'semantics':doc.get('source'),
            'transport_sha256':hashlib.sha256(w.transport(doc).encode()).hexdigest()})
        for r in result:
            if r['type']=='query':
                samples=r['whole_query_warm_ns'];s={**{k:v for k,v in r.items() if k!='whole_query_warm_ns'},'input':name,
                    'median_ns':statistics.median(samples),'min_ns':min(samples),'max_ns':max(samples),'samples_ns':samples}
                summaries.append(s)
        all_rows=[r for r in summaries if r['input']==name and r['query']=='all']
        print(name,[(r['arm'],r['operation'],round(r['median_ns'])) for r in all_rows],flush=True)
    (args.output/'summary.json').write_text(json.dumps(summaries,indent=2)+'\n')
    (args.output/'inputs.json').write_text(json.dumps(identities,indent=2)+'\n')


if __name__=='__main__':main()
