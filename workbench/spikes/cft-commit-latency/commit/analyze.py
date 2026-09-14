#!/usr/bin/env python3
"""Analyze actual quorum and all-follower times, preserving independent passes."""
import argparse
import csv
import gzip
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from summarize import stats,write_csv

FIELDS=['placement','transport','mtu','bytes','packet_bytes','retry_us','device','method','layout','filesystem','surviving_only']


def analyze(inputs,output):
    output.mkdir(parents=True,exist_ok=True)
    passes=[];groups={};nodes=[];per_run={}
    for worker,folder in inputs.items():
        require_files=['commit-cases.json','peers.json','storage-manifest.json']
        for filename in require_files:assert (folder/filename).exists(),folder/filename
        assert not (folder/'failure.json').exists(),folder
        rows=json.loads((folder/'commit-cases.json').read_text())
        nodes.append({'worker':worker,'peers':json.loads((folder/'peers.json').read_text()),
                      'storage':json.loads((folder/'storage-manifest.json').read_text())})
        for row in rows:
            per_run.setdefault(row['key'],[]).append(row)
            if row['role']=='idle':continue
            assert row['verified_records']==row['recovered_records']==row['samples']+64,row
            if row['role']!='leader':continue
            with gzip.open(folder/row['file'],'rt') as f:raw=list(csv.DictReader(f))
            assert len(raw)==row['samples'],row
            assert all(0<int(v['commit_ns'])<=int(v['all_followers_ns']) for v in raw)
            key=tuple([row['stage'],*(row[k] for k in FIELDS)])
            group=groups.setdefault(key,{'commit':[],'all':[],'local':[],'remote1':[],'remote2':[],'retries':0,'passes':[]})
            columns={'commit':'commit_ns','all':'all_followers_ns','local':'leader_durable_ns',
                     'remote1':'follower1_write_ns','remote2':'follower2_write_ns'}
            local={metric:[int(v[column])/1000 for v in raw] for metric,column in columns.items()}
            for metric,values in local.items():group[metric]+=values
            retries=sum(int(v['retried_packets']) for v in raw);group['retries']+=retries
            summary=stats(local['commit']);group['passes'].append(summary)
            passes.append({'worker':worker,**row,**summary,'retried_packets':retries,
                           **{'all_'+k:v for k,v in stats(local['all']).items()}})
    for key,rows in per_run.items():
        assert len(rows)==4,(key,len(rows))
        assert sum(row['role']=='leader' for row in rows)==1,key
        leader=next(row for row in rows if row['role']=='leader')
        assert sum(row['role']=='follower' for row in rows)==leader['remote_voters']
        assert len({row['run'] for row in rows})==1
        assert all(row['samples']==leader['samples'] for row in rows)
    pooled=[]
    for key,data in sorted(groups.items()):
        assert len(data['passes'])==(1 if key[0]=='preflight' else 3),(key,len(data['passes']))
        row=dict(zip(['stage',*FIELDS],key))|stats(data['commit'])
        row['below_1ms_pct']=100*sum(v<1000 for v in data['commit'])/len(data['commit'])
        row['retried_packets']=data['retries']
        for metric in ['all','local','remote1','remote2']:
            row.update({metric+'_'+k:v for k,v in stats(data[metric]).items()})
        for p in ['p50','p90','p99','p999']:
            row[p+'_repeat_min_us']=min(v[p+'_us'] for v in data['passes'])
            row[p+'_repeat_max_us']=max(v[p+'_us'] for v in data['passes'])
        pooled.append(row)
    write_csv(output/'commits.csv',pooled);write_csv(output/'commit-passes.csv',passes)
    (output/'commits.json').write_text(json.dumps(pooled,indent=2)+'\n')
    (output/'commit-nodes.json').write_text(json.dumps(nodes,indent=2)+'\n')
    print(json.dumps({'configurations':len(pooled),'passes':len(passes),'samples':sum(r['samples'] for r in pooled),
                      'retried_packets':sum(r['retried_packets'] for r in pooled),'verified_cohort_rounds':len(per_run)}))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('input',nargs='+',help='WORKER=RESULTS_DIRECTORY')
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    analyze({name:Path(path) for name,path in [v.split('=',1) for v in args.input]},args.output)
