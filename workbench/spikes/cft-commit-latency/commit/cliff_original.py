#!/usr/bin/env python3
"""Revisit the immutable small-cohort repeats in time order."""
import argparse,gzip,json
from pathlib import Path
import numpy as np
from cliff_analyze import quantiles,write_csv

def main(root,output):
    cases=json.loads((root/'throughput-cases.json').read_text());selected=[r for r in cases if r['stage']=='tail' and r['rate']>=100000]
    summary=[];seconds=[]
    for row in selected:
        with gzip.open(root/row['file'],'rt') as source:a=np.loadtxt(source,delimiter=',',skiprows=1,dtype=np.uint64)
        times,first=np.unique(a[:,2],return_index=True);counts=np.r_[first[1:],len(a)]
        fit=(times>=14e9)&(times<16e9);rate,burst=np.polyfit(times[fit]/1e9,counts[fit],1)
        after=a[:,0]>=1e9
        result={k:row[k] for k in ['key','rate','window','batch','transport','repeat','count']}
        result.update(late_service_rps=float(rate),late_intercept_records=float(burst),late_payload_MBps=float(rate*4096/1e6),surplus_payload_MB=float(burst*4096/1e6))
        for label,values in [('latency',a[:,2]-a[:,0]),('queue_prepare',a[:,1]-a[:,0]),('post_prepare',a[:,2]-a[:,1])]:result.update({label+'_'+k:v for k,v in quantiles(values[after]).items()})
        summary.append(result)
        for t in range(16):
            lo,hi=np.searchsorted(a[:,0],[t*1000000000,(t+1)*1000000000])
            if lo==hi:continue
            result={'key':row['key'],'second':t,'records':int(hi-lo)}
            for label,values in [('latency',a[lo:hi,2]-a[lo:hi,0]),('queue_prepare',a[lo:hi,1]-a[lo:hi,0]),('post_prepare',a[lo:hi,2]-a[lo:hi,1])]:result.update({label+'_'+k:v for k,v in quantiles(values).items()})
            seconds.append(result)
        print(row['key'],float(rate),float(burst),flush=True)
    output.mkdir(parents=True,exist_ok=True);write_csv(output/'original-cases.csv',summary);write_csv(output/'original-seconds.csv',seconds)
    (output/'original-analysis.json').write_text(json.dumps({'input':str(root),'cases':len(summary),'fit':'OLS cumulative completed record count against commit time, using the end of every equal-timestamp group in [14,16) seconds','interpretation':'empirical late service curve; intercept is an apparent surplus, not a measured hardware credit balance'},indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();main(a.root,a.output)
