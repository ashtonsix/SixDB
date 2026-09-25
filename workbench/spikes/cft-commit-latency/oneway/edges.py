#!/usr/bin/env python3
"""Retain host-edge stability across passes; optionally compare AZ2/AZ4 cohorts."""
import argparse
import csv
from pathlib import Path


def read(path):
    return list(csv.DictReader(path.open()))


def label(node):
    return f'az{node//2+1}{"ab"[node%2]}'


def summarize(folder):
    groups={}
    pairs=read(folder/'pair-blocks.csv')
    for row in read(folder/'blocks.csv'):
        a,b=int(row['src']),int(row['dst'])
        if a//2 != b//2:
            groups.setdefault((a,b),[]).append(row)
    result=[]
    for (a,b),rows in sorted(groups.items()):
        assert len(rows)==4 and {int(r['repeat']) for r in rows}==set(range(4))
        pair=[r for r in pairs if (int(r['node_a']),int(r['node_b']))==tuple(sorted((a,b)))]
        out=dict(src=a,dst=b,edge=label(a)+' -> '+label(b),passes=len(rows))
        for field in ('p50_us','p99_us','p50_lo_us','p50_hi_us','p99_hi_us'):
            values=[float(r[field]) for r in rows]
            out[field+'_min']=min(values);out[field+'_max']=max(values)
        for field in ('rtt_p50_us','rtt_p99_us'):
            values=[float(r[field]) for r in pair]
            out[field+'_min']=min(values);out[field+'_max']=max(values)
        out['passes_point_p50_le_110']=sum(float(r['p50_us'])<=110 for r in rows)
        out['passes_point_p99_le_110']=sum(float(r['p99_us'])<=110 for r in rows)
        out['passes_bounded_p50_le_110']=sum(float(r['p50_hi_us'])<=110 for r in rows)
        out['passes_resolved_faster']=sum(float(r['p50_hi_us'])<0 if a<b else float(r['p50_lo_us'])>0 for r in pair)
        result.append(out)
    with (folder/'host-edges.csv').open('w') as f:
        w=csv.DictWriter(f,fieldnames=list(result[0]));w.writeheader();w.writerows(result)


def compare(first,second):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11})
    fig,axes=plt.subplots(2,2,figsize=(11,7.8),layout='constrained')
    labels=['2a → 4a','2a → 4b','2b → 4a','2b → 4b']
    for i,(folder,title) in enumerate(((first,'First hosts'),(second,'Fresh hosts'))):
        blocks=read(folder/'blocks.csv');pairs=read(folder/'pair-blocks.csv')
        for j,(field,rows) in enumerate((('p50_us',blocks),('rtt_p50_us',pairs))):
            data=np.empty((4,4))
            for n,(a,b) in enumerate(((2,6),(2,7),(3,6),(3,7))):
                selected=[r for r in rows if (int(r['src']),int(r['dst']))==(a,b)] if j==0 else [r for r in rows if (int(r['node_a']),int(r['node_b']))==(a,b)]
                for r in selected:data[n,int(r['repeat'])]=float(r[field])
            ax=axes[i,j]
            ax.imshow(data,cmap='YlOrRd',vmin=90 if j==0 else 200,vmax=260 if j==0 else 500,aspect='auto')
            for n in range(4):
                for k in range(4):
                    ax.text(k,n,f'{data[n,k]:.1f}',ha='center',va='center',color='white' if data[n,k]>(210 if j==0 else 410) else '#222222')
            ax.set_xticks(range(4),[f'Pass {k}' for k in range(4)])
            ax.set_yticks(range(4),labels)
            ax.set_title(title+(' · forward median estimate' if j==0 else ' · clock-independent RTT median'),loc='left',fontsize=12)
    fig.suptitle('A fast AZ2–AZ4 edge depends on the hosts and flow\nEvery cell is a separate, newly opened UDP flow · microseconds',fontsize=16,weight='bold')
    fig.supxlabel('One-way cells depend on independent clock fits; their error bounds are not shown here.\nHost labels identify different machines in the two cohorts. RTT excludes peer turnaround.',fontsize=10)
    fig.savefig(second/'az2-az4.png',dpi=160)
    fig.savefig(second/'az2-az4.svg')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('summary',type=Path)
    p.add_argument('--compare-first',type=Path)
    a=p.parse_args()
    summarize(a.summary)
    if a.compare_first:
        compare(a.compare_first,a.summary)
