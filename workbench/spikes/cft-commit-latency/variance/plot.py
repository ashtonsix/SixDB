#!/usr/bin/env python3
"""Scientific figures for flow rerolls and their repeatability."""
import argparse
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def read(path):
    return list(csv.DictReader(path.open()))


def dense(folder):
    rows=read(folder/'flow-blocks.csv')
    colors=['#236A92','#BF5F2F','#65833B','#87558E']
    fig,axes=plt.subplots(1,2,figsize=(12,5.6),layout='constrained',sharey=True)
    for ax,(a,b,title) in zip(axes,[(2,4,'az2c / az4a: port tuple changes RTT'),(0,6,'az2a / az4c: every tested tuple is fast')]):
        for round_ in range(4):
            selected=sorted((r for r in rows if int(r['src'])==a and int(r['dst'])==b and int(r['round'])==round_),key=lambda r:int(r['flow']))
            ax.scatter([int(r['flow'])+(round_-1.5)*.15 for r in selected],
                       [float(r['rtt_p50_us']) for r in selected],s=30,color=colors[round_],label=f'Round {round_}')
        ax.set_title(title,loc='left',weight='bold',fontsize=12)
        ax.set_xticks(range(16));ax.set_xlabel('Tuple index (same endpoint ports in every round)')
        ax.grid(axis='y',alpha=.25);ax.set_axisbelow(True)
        ax.spines[['top','right']].set_visible(False)
    axes[0].set_ylabel('Clock-independent network RTT median (µs)')
    axes[1].legend(ncol=2,loc='upper left',fontsize=9)
    fig.suptitle('Port-based rerolls are repeatable on the same hosts',fontsize=17,weight='bold')
    fig.supxlabel('64-byte UDP · sockets reopened between shuffled rounds · initiator alternates · peer turnaround excluded\nA tuple-sensitive mechanism is observed; physical route and IID sampling are not established.',fontsize=10)
    fig.savefig(folder/'tuple-repeatability.png',dpi=160);fig.savefig(folder/'tuple-repeatability.svg')


def broad(folder):
    rows=read(folder/'selected-az-directions-request.csv')
    assert all(r['rank_mode']=='request' for r in rows)
    fig,ax=plt.subplots(figsize=(13,8),layout='constrained')
    for index,r in enumerate(rows):
        point=float(r['holdout_request_p50_us']);lo=float(r['holdout_request_p50_lo_us']);hi=float(r['holdout_request_p50_hi_us'])
        color='#969A9E' if '3' in (r['az_src'],r['az_dst']) else '#236A92'
        ax.plot([lo,hi],[index,index],color=color,alpha=.6,lw=2)
        ax.scatter([point],[index],s=25,color=color)
        ax.scatter([float(r['train_worst_p50_us'])],[index],marker='x',s=28,color='#BF5F2F')
    ax.set_yticks(range(len(rows)),[f"az{r['az_src']} → az{r['az_dst']}" for r in rows]);ax.invert_yaxis()
    ax.axvline(110,color='#333333',ls='--',lw=1)
    ax.grid(axis='x',alpha=.25);ax.set_axisbelow(True)
    ax.set_xlabel('One-way median estimate (µs); bars are conditional clock-error intervals')
    ax.set_title('A host and port selected on training rounds, then tested on a held-out request\nOrange ×: worst training request median · dot: held-out request median · dashed: 110 µs',loc='left',fontsize=14,weight='bold')
    ax.spines[['top','right']].set_visible(False)
    fig.supxlabel('Two hosts per AZ · four tuples per pair · gray: NTP-bound az3 · kernel TX→RX, without PLP writes or concurrent fanout',fontsize=10)
    fig.savefig(folder/'heldout-edges.png',dpi=160);fig.savefig(folder/'heldout-edges.svg')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('summary',type=Path);p.add_argument('--broad',action='store_true');a=p.parse_args()
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10})
    (broad if a.broad else dense)(a.summary)
