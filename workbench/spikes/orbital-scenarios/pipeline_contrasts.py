#!/usr/bin/env python3
"""Discriminating pipeline cases with explicit drain and deadline contracts."""

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from comparison_inputs import cases, _case, _transaction
from pipeline_simulation import PipelineSimulation, identity


def blind_overlap(delay=150):
    initial = {'eu/x': 0, 'us/y': 0, 'eu/other': 0}
    txs = [_transaction('wan', 0, 'wan', 'eu', [], ['eu/x','us/y'], 'blind', value=1, delay=delay)]
    for number in range(96):
        txs.append(_transaction(f'blind-{number}',60+2*number,'blind','eu',[],['eu/x'],'blind',value=number+2))
        txs.append(_transaction(f'other-{number}',60+2*number,'regional','eu',[],['eu/other'],'blind',value=number+2))
    return _case('blind_full_replacement_overlap','Unconditional complete-value puts; no existence, constraint, old-value or RETURNING reads.',
                 initial,{},txs,horizon=3000,read_wait_ticks=20000,capacity=32,link_delay=20)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    sources={**identity(),'pipeline_contrasts.py':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    selected=[]
    for case in cases(seed=7,width=64):
        if case['name'] in ('ordinary_distributed_hot','narrow_conflicting_wan','bulk_update_point_updates'):
            original=deepcopy(case)
            original['retry_limit']=1
            selected.append(('single-attempt-deadline',original))
            case['read_wait_ticks']=20000
            case['horizon']=20000
            case['retry_limit']=1
            selected.append(('long-drain-no-retry',case))
    selected += [(f'blind-delay-{delay}',blind_overlap(delay)) for delay in (150,600)]
    rows=[]
    for variant,case in selected:
        for release in (False,True):
            sim=PipelineSimulation(deepcopy(case),release)
            row=sim.run()
            row.update(variant=variant,input=case)
            if variant.startswith('long-drain') or variant.startswith('blind-delay'):
                assert all(c['complete']==c['offered'] for c in row['cohorts'].values())
            rows.append(row)
            print(variant,case['name'],row['policy'],{g:(c['complete'],c['failed'],c['pending'],c['p99_ticks'])
                for g,c in row['cohorts'].items()},flush=True)
    assert sources=={**identity(),'pipeline_contrasts.py':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps({'source_sha256':sources,'rows':rows,
        'limits':['Same synthetic scheduler as pipeline sweep.',
                  'One attempt for timed deadline/drain controls; no automatic resource retry.',
                  'Blind replacements are explicitly unconstrained unconditional complete-value puts, not ordinary SQL UPDATE.']},
        indent=2,sort_keys=True)+'\n')


if __name__=='__main__':
    main()
