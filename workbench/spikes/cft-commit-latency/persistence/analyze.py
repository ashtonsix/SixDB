#!/usr/bin/env python3
"""Pool raw persistence samples, retaining pass spread and device identity."""
import argparse
import csv
import gzip
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from summarize import stats,write_csv


def analyze(inputs,output):
    output.mkdir(parents=True,exist_ok=True)
    groups={};passes=[];devices={}
    for name,folder in sorted(inputs.items()):
        manifest=json.loads((folder/'storage-manifest.json').read_text())
        devices[name]={'manifest':manifest,'receipts':{}}
        cases=json.loads((folder/'storage-cases.json').read_text())
        for path in sorted(folder.glob('*-before.json')):
            devices[name]['receipts'][path.stem]=json.loads(path.read_text())
        for case in cases:
            with gzip.open(folder/case['file'],'rt') as f:
                values=[int(row['latency_ns'])/1000 for row in csv.DictReader(f)]
            assert len(values)==case['samples']
            assert case['verified_records']==case['samples']+case['warmup']
            key=(name,*(case[k] for k in ['device','stage','phase','method','layout','bytes']))
            entry=groups.setdefault(key,{'values':[],'passes':[]})
            summary=stats(values);entry['values']+=values;entry['passes'].append(summary)
            passes.append({'worker':name,**case,**summary})
    pooled=[]
    for key,data in sorted(groups.items()):
        assert len(data['passes'])==3,(key,len(data['passes']))
        row=dict(zip(['worker','device','stage','phase','method','layout','bytes'],key))|stats(data['values'])
        row['below_1ms_pct']=100*sum(v<1000 for v in data['values'])/len(data['values'])
        for percentile in ['p50','p90','p99','p999']:
            row[percentile+'_repeat_min_us']=min(p[percentile+'_us'] for p in data['passes'])
            row[percentile+'_repeat_max_us']=max(p[percentile+'_us'] for p in data['passes'])
        pooled.append(row)
    write_csv(output/'persistence.csv',pooled)
    write_csv(output/'persistence-passes.csv',passes)
    (output/'devices.json').write_text(json.dumps(devices,indent=2)+'\n')
    (output/'persistence.json').write_text(json.dumps(pooled,indent=2)+'\n')
    print(json.dumps({'devices':sum(len(d['manifest']['ebs'])+bool(d['manifest']['instance_store']) for d in devices.values()),
                      'cases':len(pooled),'passes':len(passes),'samples':sum(r['samples'] for r in pooled)}))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',nargs='+',help='WORKER=RESULTS_DIRECTORY')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    analyze({name:Path(path) for name,path in [v.split('=',1) for v in args.input]},args.output)
