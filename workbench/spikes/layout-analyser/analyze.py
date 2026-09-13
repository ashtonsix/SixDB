#!/usr/bin/env python3
"""Summarize native consumer repetitions without mixing workloads or contexts."""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
from statistics import median


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('evidence',type=Path)
    args=ap.parse_args()
    rows=[]
    for group in json.loads((args.evidence/'cases.json').read_text()):
        observations=defaultdict(list)
        for row in csv.DictReader((args.evidence/group['samples']).open()):
            assert int(row['rows'])==group['rows'] and int(row['seed'])==group['seed']
            observations[row['case']].append(row)
        for name,repetitions in observations.items():
            ns=[float(row['ns_per_operation']) for row in repetitions]
            assert all(x>0 for x in ns)
            rows.append(dict(family=group['family'],case=name,rows=group['rows'],seed=group['seed'],
                repetitions=len(ns),median_ns=median(ns),min_ns=min(ns),max_ns=max(ns),
                allocated_bytes=int(repetitions[0]['allocated_bytes']),queries=group['queries'],
                conditional_visits=int(repetitions[0]['conditional_visits'])))
    with (args.evidence/'summary.csv').open('w') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    lines=['# Native consumer observations','',
        'Median elapsed ns per completed operation on the pinned CPU. Seeds and working sets remain separate. Repeated traces are warmed; allocation size does not establish a cache tier.','',
        '| Family / rows / seed | Cases | Minimum / median / maximum case median, ns |',
        '| --- | ---: | ---: |']
    for key in sorted({(r['family'],r['rows'],r['seed']) for r in rows}):
        values=[r['median_ns'] for r in rows if (r['family'],r['rows'],r['seed'])==key]
        lines.append(f'| {key[0]} / {key[1]} / {key[2]} | {len(values)} | {min(values):.3f} / {median(values):.3f} / {max(values):.3f} |')
    lines += ['', 'This coverage summary is not a ranking across different requested operations. Compare matching operations and logical data. Raw controls omit the ordinary API and its effects; raw-format fingerprint arms include layouts that SeriesPack cannot currently admit. See the study source, metadata and per-case rows.']
    (args.evidence/'summary.md').write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines))


if __name__=='__main__':main()
