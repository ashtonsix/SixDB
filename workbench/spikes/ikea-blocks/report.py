#!/usr/bin/env python3
"""Compare sequential repetition medians; timings are nanoseconds, not cycles."""
import argparse
import csv
import statistics
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from evidence import verify_compact

def medians(path,key,value):
    groups={}
    with path.open() as stream:
        for row in csv.DictReader(stream):
            groups.setdefault(tuple(row[k] for k in key),[]).append(float(row[value]))
    return {k:statistics.median(v) for k,v in groups.items()}

def report(directory):
    if (directory/'artifact.json').exists(): verify_compact(directory)
    if not (directory/'timings.csv').exists(): return []
    times=medians(directory/'timings.csv',['corpus','operation'],'ns_per_cell')
    comparisons=[('encode','bec_encode','calico_encode'),('encode_p2','bec_encode','calico_p2_encode'),
                 ('decode','bec_decode','calico_decode'),('decode_x2','bec_decode_x2','calico_decode_x2')]
    rows=[]
    for name,ours,prior in comparisons:
        for (corpus,op),ns in times.items():
            if op!=ours or (corpus,prior) not in times: continue
            baseline=times[corpus,prior]
            rows.append({'comparison':name,'corpus':corpus,'bec_ns':ns,'prior_ns':baseline,'ratio':ns/baseline})
    return rows

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directories',nargs='+',type=Path)
    p.add_argument('--csv',type=Path)
    a=p.parse_args()
    output=[]
    for directory in a.directories:
        rows=report(directory)
        print(directory)
        if not rows:
            print((directory/'checks.csv').read_text().strip())
        for comparison in sorted({row['comparison'] for row in rows}):
            selected=[r for r in rows if r['comparison']==comparison and (r['corpus'].endswith('/partial') or '/' not in r['corpus'])]
            ratios=[r['ratio'] for r in selected]
            worst=max(selected,key=lambda r:r['ratio'])
            print(f"  {comparison}: {len(ratios)} cases, median {statistics.median(ratios):.3f}x prior time; range {min(ratios):.3f}–{max(ratios):.3f}; worst {worst['corpus']}")
        output.extend({'run':directory.name,**row} for row in rows)
        path=directory/'composition-timings.csv'
        if path.exists():
            values=medians(path,['case','layout'],'ns_per_tile')
            for (case,layout),ns in values.items():
                if layout=='contiguous': print(f'  {case}: {ns:.3f} ns/tile')
    if a.csv:
        with a.csv.open('w') as stream:
            writer=csv.DictWriter(stream,fieldnames=list(output[0]))
            writer.writeheader();writer.writerows(output)
