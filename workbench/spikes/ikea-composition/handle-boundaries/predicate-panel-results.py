#!/usr/bin/env python3
"""Read the one-image op2 panel without pooling cases or correcting duplicates."""
import argparse
import csv
import hashlib
import itertools
import json
import math
from pathlib import Path
import statistics

ARMS=('before','after','duplicate')
ORDERS=[['before','after','duplicate'],['after','duplicate','before'],['duplicate','before','after'],
        ['duplicate','after','before'],['after','before','duplicate']]
HEADER='workload,width_or_cases,op,active,candidate,metric,rep,ns_window'
FIELDS=HEADER.split(',')
CASES={'/'.join((work,width,'2',active,route,mode))
       for (work,width),active,route,mode in itertools.product(
           (('tuple_tight','5'),('tuple_tight','8'),('tuple_strided','5')),
           ('65535','42300'),('bound_fused','one_stage','shared','inline_fused'),('dependent','independent8'))}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stats(values):
    center=statistics.median(values)
    return {'samples':values,'median':center,'min':min(values),'max':max(values),
            'mad':statistics.median(abs(value-center) for value in values)}


def parse(path):
    lines=path.read_text().splitlines()
    assert lines[0]==HEADER
    values={};checksums=[];order=[];current=None;rows={}
    for line in lines[1:]:
        if line.startswith('# begin,'):
            assert current is None
            _,trial,arm=line.split(',');current=(int(trial),arm)
            assert current not in values and current[1] in ARMS
            rows={};order.append(current)
        elif line.startswith('# end,'):
            _,trial,arm,checksum=line.split(',')
            assert current==(int(trial),arm) and set(rows)==CASES,(path,current,set(rows)^CASES)
            values[current]=rows;checksums.append(int(checksum));current=None
        else:
            assert current is not None and line,line
            row=next(csv.DictReader([HEADER,line]))
            assert set(row)==set(FIELDS) and row['rep']=='0'
            key='/'.join(row[k] for k in FIELDS[:6])
            number=float(row['ns_window'])
            assert key in CASES and key not in rows and math.isfinite(number) and number>0,(key,number)
            rows[key]=number
    assert current is None and order==[(i,arm) for i,arms in enumerate(ORDERS) for arm in arms],order
    assert len(set(checksums))==1 and checksums[0]>0,checksums
    return values,checksums[0]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results',type=Path,required=True)
    args=parser.parse_args()
    folder=args.results
    receipt=json.loads((folder/'receipt.json').read_text())
    build=json.loads((folder/'build-receipt.json').read_text())
    identity=json.loads((folder/'identity.json').read_text())
    assert receipt['complete'] and identity['complete'] and build['complete']
    assert receipt['build_receipt_sha256']==sha(folder/'build-receipt.json')
    assert receipt['identity_receipt_sha256']==sha(folder/'identity.json')
    assert all(row['exit_code']==0 for row in receipt['commands'])
    result={'scope':__doc__,'complete':False,'receipt_sha256':sha(folder/'receipt.json'),
            'identity_sha256':sha(folder/'identity.json'),'source_manifest_sha256':build['source_manifest_sha256'],
            'runner_sha256':sha(Path(__file__)),'profiles':{},
            'limits':['One linked image with distinct namespace-isolated code and state, not the original independent-process comparison.',
                      'Before duplicate is separately emitted equivalent code at different addresses. It is not an identical-ELF copy.',
                      'Five within-process trials describe this panel; they are not five independent process launches.',
                      'No duplicate correction, pooled throughput score or causal instruction-count interpretation.']}
    table=[]
    for profile in build['profiles']:
        path=folder/profile/'panel.csv'
        recorded=next(row for row in receipt['commands'] if row['name']=='timing-'+profile)
        assert sha(path)==recorded['log_sha256']
        stderr=folder/profile/'panel.stderr.log'
        assert sha(stderr)==recorded['stderr_sha256']
        assert stderr.read_text()=='predicate-panel: actual op2 fixture oracles and cross-arm consumed checksums passed\n'
        values,checksum=parse(path)
        cases={}
        for key in sorted(CASES):
            samples={arm:[values[(trial,arm)][key] for trial in range(5)] for arm in ARMS}
            ratios={arm:[a/b for a,b in zip(samples[arm],samples['before'])] for arm in ('after','duplicate')}
            cases[key]={'wall_ns_per_window':{arm:stats(v) for arm,v in samples.items()},
                        'paired_after_over_before':stats(ratios['after']),
                        'paired_duplicate_over_before':stats(ratios['duplicate'])}
            table.append({'profile':profile,'case':key,
                          **{arm+'_ns_median':statistics.median(samples[arm]) for arm in ARMS},
                          **{arm+'_samples':json.dumps(samples[arm]) for arm in ARMS},
                          **{arm+'_over_before_samples':json.dumps(ratios[arm]) for arm in ('after','duplicate')},
                          **{arm+'_over_before_median':statistics.median(ratios[arm]) for arm in ('after','duplicate')}})
        result['profiles'][profile]={'csv_sha256':sha(path),'cases':cases,'consumed_checksum':checksum}
    result['complete']=True
    (folder/'summary.json').write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    with (folder/'paired-cases.csv').open('w',newline='') as handle:
        writer=csv.DictWriter(handle,fieldnames=list(table[0]));writer.writeheader();writer.writerows(table)
    print('one-image panel parsed',len(table),'case metrics',flush=True)


if __name__=='__main__':
    main()
