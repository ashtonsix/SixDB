#!/usr/bin/env python3
"""Static figures for the cliff's cause, queue amplification and controller tradeoffs."""
import argparse,csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.spines.top':False,'axes.spines.right':False})
COLORS=['#157a91','#df9634','#b44d5d','#7256a8']
PERCENTILE_COLORS=['#157a91','#49a89b','#df9634','#b44d5d']
MARKERS=['o','s','D','^']
def read(path):return list(csv.DictReader(path.open()))
def values(rows,key):return [float(r[key]) for r in rows]
def finish(fig,path,caption):
    fig.text(.02,.016,caption,fontsize=8,color='#52616b',va='bottom')
    fig.savefig(path,dpi=180);plt.close(fig)

def small(root,out):
    seconds=read(root/'seconds.csv');cases=read(root/'cases.csv')
    fig,axes=plt.subplots(3,1,figsize=(10.4,8.8),sharex=True)
    rows=[r for r in seconds if r['key']=='00-original-16s-r0'];x=np.array(values(rows,'second'))+.5
    for field,label,color in [('latency_p99_ms','Arrival → commit',COLORS[2]),('queue_p99_ms','Before payload preparation',COLORS[1]),('post_prepare_p99_ms','Prepared → commit',COLORS[0])]:axes[0].plot(x,values(rows,field),label=label,color=color,lw=2,marker='.')
    axes[0].set_yscale('log');axes[0].set_ylabel('p99 (ms, log scale)');axes[0].legend(loc='upper left',fontsize=9)
    for field,label,color in [('local_p99_ms','Leader durable-write CQE',COLORS[2]),('ack0_p99_ms','AZ2 durable ACK',COLORS[0]),('ack1_p99_ms','AZ1 durable ACK',COLORS[3])]:axes[1].plot(x,values(rows,field),label=label,color=color,lw=2,marker='.')
    axes[1].set_ylabel('After preparation\np99 (ms)');axes[1].legend(loc='upper left',fontsize=9)
    axes[2].plot(x,values(rows,'below_1ms_pct'),color=COLORS[0],lw=2,marker='.')
    axes[2].set_ylim(0,105);axes[2].set_ylabel('Commits below 1 ms (%)');axes[2].set_xlabel('Scheduled arrival time (seconds)')
    for ax in axes:ax.grid(alpha=.2)
    axes[0].set_title('A local write slowdown becomes a much larger admission queue',loc='left',weight='bold',pad=14)
    fig.subplots_adjust(left=.12,right=.98,top=.93,bottom=.10,hspace=.22)
    finish(fig,out/'cliff-path.png','Fresh i8g.large, TCP W64/B4, 104,400 offered/s; 1-second arrival cohorts. Stage percentiles are separately ranked.\nAll ENA allowance counters stayed zero in this pass. The same-record prefix check identifies the leader as last for the slowest 1%.')

    groups=[('Original, 16 s',['00-original-16s-r0']),('Original, 60 s',['01-original-60s-r0']),('Skip zero-fill, 60 s',['02-skip-initialize-60s-r0','08-skip-initialize-60s-r1']),('Fixed B16, 60 s',['04-batch16-104k-r0']),('Adaptive B4→64, 60 s',['05-adaptive-batch-104k-r0','09-adaptive-batch-104k-r1']),('90,000/s, 60 s',['03-below-cliff-90k-r0','07-below-cliff-90k-r1']),('15,000/s, 60 s',['06-baseline-network-15k-r0'])]
    fig,axes=plt.subplots(1,2,figsize=(11.2,5.9),sharey=True,gridspec_kw={'width_ratios':[1.6,1]})
    for y,(label,keys) in enumerate(groups):
        subset=[r for r in cases if r['key'] in keys]
        for offset,p,color,marker in zip([-.15,.15],['latency_p99_ms','latency_p999_ms'],PERCENTILE_COLORS[2:],MARKERS[2:]):
            a=values(subset,p);axes[0].plot([min(a),max(a)],[y+offset]*2,color=color,lw=2)
            axes[0].scatter(a,[y+offset]*len(a),color=color,marker=marker,s=30,label=p.replace('latency_','').replace('_ms','').replace('p999','p99.9') if y==0 else None)
        for row in subset:axes[1].scatter(float(row['goodput_rps'])/1000,y-.13,color='#adb8bf',s=35);axes[1].scatter(float(row['deadline_goodput_rps'])/1000,y+.13,color=COLORS[0],s=35)
    axes[0].set_yticks(range(len(groups)),[g[0] for g in groups]);axes[0].invert_yaxis();axes[0].set_xscale('log');axes[0].axvline(1,color='#666',ls='--',lw=1);axes[0].set_xlabel('Arrival → commit (ms, log scale)');axes[0].legend(frameon=False,loc='lower right')
    axes[1].set_xlabel('Thousand commits/s');axes[1].scatter([],[],color='#adb8bf',label='All commits');axes[1].scatter([],[],color=COLORS[0],label='Below 1 ms');axes[1].legend(frameon=False,loc='lower right',fontsize=9)
    for ax in axes:ax.grid(axis='x',alpha=.2)
    fig.suptitle('Reducing overload preserves far more on-time work than changing batch size',x=.02,ha='left',weight='bold',fontsize=12)
    fig.subplots_adjust(left=.24,right=.98,top=.90,bottom=.16,wspace=.18)
    finish(fig,out/'cliff-choices.png','Points are individual passes, excluding the first scheduled second; ranges connect only repeats. No rejected arrivals.\nThe 90k/s point exceeds this small instance’s network baseline; it is a 60-second observation, not a sustained SLA capacity.')

def local(root,out):
    nvme=read(root/'nvme.csv');seconds=read(root/'seconds.csv')
    fig,axes=plt.subplots(3,1,figsize=(10.4,8.4),sharex=True)
    for key,label,color in [('00-local-90000-b4','90,000/s, B4',COLORS[0]),('01-local-104400-b4','104,400/s, B4',COLORS[2]),('03-local-104400-b16','104,400/s, B16',COLORS[1])]:
        style='--' if key.endswith('b16') else '-'
        n=[r for r in nvme if r['key']==key and 0<=float(r['second'])<=40]
        s=[r for r in seconds if r['key']==key]
        axes[0].plot(values(n,'second'),[float(r['write_bytes'])/float(r['interval_seconds'])/1e6 for r in n],color=color,label=label,lw=1.7,ls=style)
        axes[1].plot(values(n,'second'),[float(r['instance_tp_exceeded_us'])/float(r['interval_seconds'])/1e4 for r in n],color=color,lw=1.7,ls=style)
        axes[2].plot(np.array(values(s,'second'))+.5,values(s,'latency_p99_ms'),color=color,lw=1.7,ls=style)
    axes[0].axhline(412.9,color='#777',ls='--',lw=1);axes[0].set_ylabel('Device writes (MB/s)');axes[0].legend(ncol=3,fontsize=9,loc='lower left')
    axes[1].set_ylabel('Throughput exceeded\n(% of counter interval)');axes[1].set_ylim(-3,105)
    axes[2].set_yscale('log');axes[2].set_ylabel('Arrival → local durability\np99 (ms, log scale)');axes[2].set_xlabel('Seconds from the arrival schedule start')
    for ax in axes:ax.grid(alpha=.2)
    axes[0].set_title('The ceiling survives removal of every network operation',loc='left',weight='bold',pad=14)
    fig.subplots_adjust(left=.16,right=.98,top=.93,bottom=.10,hspace=.22)
    finish(fig,out/'cliff-nvme.png','Fresh i8g.large local-only control: same raw O_DIRECT|O_DSYNC write/ring machinery, 4 KiB records.\nAWS NVMe vendor counters sampled no faster than 1.1 s. IOPS-exceeded counters were zero. These are not CFT commits.')

def express(root,out):
    cases=read(root/'cases.csv')
    groups=[('Fully prepared file','initialized-129k'),('Prepare during appends','prep-always-129k'),('Adaptive preparation','prep-adaptive-129k'),('Adaptive prep + batch','prep-and-batch-adaptive-129k')]
    fig,axes=plt.subplots(1,2,figsize=(11.1,5.8),gridspec_kw={'width_ratios':[1.5,1]})
    for i,(label,prefix) in enumerate(groups):
        rows=[r for r in cases if r['name'].startswith(prefix)]
        for j,(percentile,color,marker) in enumerate(zip(['p50','p90','p99','p999'],PERCENTILE_COLORS,MARKERS)):
            v=values(rows,'latency_'+percentile+'_ms');x=i+(j-1.5)*.14
            axes[0].plot([x,x],[min(v),max(v)],color=color,lw=2);axes[0].scatter([x]*len(v),v,c=color,marker=marker,s=26,label=percentile.replace('p999','p99.9') if i==0 else None)
        axes[1].scatter([i]*len(rows),[100-x for x in values(rows,'below_1ms_pct')],c=COLORS[2],s=34)
    for ax in axes:ax.set_xticks(range(4),[x[0].replace(' ','\n',1) for x in groups],fontsize=9);ax.grid(axis='y',alpha=.2)
    axes[0].set_yscale('log');axes[0].axhline(1,color='#666',ls='--',lw=1);axes[0].set_ylabel('Arrival → commit (ms, log scale)');axes[0].legend(ncol=4,frameon=False,loc='upper left',fontsize=9)
    axes[1].set_ylim(0,25);axes[1].set_ylabel('Post-warmup arrival cohort missing 1 ms (%)')
    fig.suptitle('Preparation and batching controllers must earn their tail-latency claim',x=.02,ha='left',weight='bold',fontsize=12)
    fig.subplots_adjust(left=.09,right=.97,top=.90,bottom=.24,wspace=.28)
    finish(fig,out/'cliff-preparation.png','i8g.8xlarge + ENA Express; 129,600 offered/s, 60 s/pass, two passes/policy; first scheduled second excluded.\nAll offered records finish and pass three-voter readback. Combined control changes both preparation and B4→64 at W64.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    if (a.root/'analysis-small/cases.csv').exists():small(a.root/'analysis-small',a.output)
    if (a.root/'analysis-local/cases.csv').exists():local(a.root/'analysis-local',a.output)
    if (a.root/'analysis-express/cases.csv').exists():express(a.root/'analysis-express',a.output)
