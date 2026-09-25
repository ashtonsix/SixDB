#!/usr/bin/env python3
"""Plot directional estimates together with clock uncertainty and host variation."""
import argparse
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def rows(path):
    return list(csv.DictReader(path.open()))


def main():
    p=argparse.ArgumentParser()
    p.add_argument('evidence',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--confirmation',action='store_true')
    a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True)
    directions={(int(r['az_src']),int(r['az_dst'])):r for r in rows(a.evidence/'directions.csv')}
    pairs=[r for r in rows(a.evidence/'asymmetry.csv') if r['az_a']!=r['az_b']]
    blocks=rows(a.evidence/'pair-blocks.csv')
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'axes.spines.top':False,'axes.spines.right':False})
    fig,(left,right)=plt.subplots(1,2,figsize=(13,9),gridspec_kw={'width_ratios':[1.25,1]},layout='constrained')
    for index,r in enumerate(pairs):
        x,y=int(r['az_a']),int(r['az_b'])
        if 3 in (x,y):
            for ax in (left,right): ax.axhspan(index-.45,index+.45,color='#f0f1f3',zorder=0)
        for delta,key,color,label in [(-.13,(x,y),'#176B87','Lower AZ → higher AZ'),(.13,(y,x),'#D36B38','Higher AZ → lower AZ')]:
            s=directions[key]
            point,lo,hi=[float(s[k]) for k in ('p50_us','p50_lo_us','p50_hi_us')]
            left.plot([lo,hi],[index+delta]*2,color=color,lw=2,alpha=.65)
            left.scatter([point],[index+delta],s=26,color=color,label=label if index==0 else None,zorder=3)
        point,lo,hi=[float(r[k]) for k in ('p50_us','p50_lo_us','p50_hi_us')]
        right.plot([lo,hi],[index]*2,color='#5A5874',lw=3,alpha=.7)
        right.scatter([point],[index],s=30,color='#343044',zorder=4)
        points=[float(b['p50_us']) for b in blocks if int(b['az_a'])==x and int(b['az_b'])==y]
        right.scatter(points,[index]*len(points),s=10,color='#8D899E',alpha=.55,zorder=2)
    labels=[f"az{r['az_a']}–az{r['az_b']}" for r in pairs]
    for ax in (left,right):
        ax.set_yticks(range(len(pairs)),labels)
        ax.invert_yaxis(); ax.grid(axis='x',color='#dddddd',alpha=.7)
        ax.set_axisbelow(True)
    left.set_title('One-way median with clock-error interval',loc='left',weight='bold')
    left.set_xlabel('Microseconds · software TX → software RX')
    left.legend(loc='lower right',fontsize=9)
    right.axvline(0,color='#333333',lw=1,ls='--')
    right.set_title('Paired directional difference',loc='left',weight='bold')
    right.set_xlabel('Forward − reverse (µs) · faint dots: host/pass medians')
    fig.suptitle('Cross-AZ latency: clock uncertainty stays visible\nTwo hosts per AZ · four host combinations · four balanced passes',fontsize=17,weight='bold')
    note = 'Shading: az3 (NTP). ' + ('' if a.confirmation else 'One az2 host also needs NTP fallback. ')
    fig.supxlabel(note+'Intervals are clock-error bounds, not confidence intervals.',fontsize=10)
    fig.savefig(a.output/'oneway.png',dpi=160)
    fig.savefig(a.output/'oneway.svg')


if __name__=='__main__': main()
