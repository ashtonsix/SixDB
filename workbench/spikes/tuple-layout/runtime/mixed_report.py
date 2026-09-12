#!/usr/bin/env python3
"""Compare isolated-cost prediction with measured mixed-plan choice, offline."""
import argparse
import csv
import json
from pathlib import Path
from statistics import median
from report import read, verify_compact

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('evidence', type=Path, nargs='+')
ap.add_argument('--json-output', type=Path)
args = ap.parse_args()
all_results = []
for root in args.evidence:
    verify_compact(root)
    samples = sorted(root.glob('*/samples.csv'))
    if not samples:
        raise ValueError(f'No sample CSVs in {root}; use runtime/recover.py '
                         'to restore archived evidence (see evidence/README.md).')
    for path in samples:
        times = read(path)
        weights = {}
        for row in csv.DictReader(path.open()):
            if row['name'].startswith('mixture/'):
                mix = row['name'].split('/')[2]
                observed = {k[len('fraction_'):]:float(v) for k,v in row.items() if k.startswith('fraction_') and v}
                if mix in weights and weights[mix] != observed:
                    raise ValueError('case weights changed inside one mixture')
                weights[mix] = observed
        print(f'\n{root.name}/{path.parent.name}: measured mixed plans, ns/invocation')
        print('| Nominal writes | Predicted choice measured | Best measured | Choice regret | Actual/additive median |')
        print('| ---: | ---: | ---: | ---: | ---: |')
        for mix, observed in sorted(weights.items(),key=lambda x:int(x[0])):
            plans = []
            for name, actual in times.items():
                bits = name.split('/')
                if len(bits) != 4 or bits[0] != 'mixture' or bits[2] != mix:
                    continue
                candidate, family = bits[1],bits[3]
                predicted = sum(weight*times[f'small/{candidate}/{op}/{family}'] for op,weight in observed.items())
                plans.append({'candidate':candidate,'family':family,'predicted_ns':predicted,'actual_ns':actual})
            predicted = min(plans,key=lambda p:(p['predicted_ns'],p['candidate'],p['family']))
            best = min(plans,key=lambda p:(p['actual_ns'],p['candidate'],p['family']))
            regret = predicted['actual_ns']/best['actual_ns']-1
            ratio = median(p['actual_ns']/p['predicted_ns'] for p in plans)
            print(f"| {mix}% | {predicted['actual_ns']:.3f} | {best['actual_ns']:.3f} | {100*regret:.2f}% | {ratio:.2f}× |")
            all_results.append({'evidence':str(root),'profile':path.parent.name,'nominal_write_percent':int(mix),
                'observed_weights':observed,'plan_count':len(plans),'predicted_choice':predicted,'measured_choice':best,
                'choice_regret':regret,'actual_to_additive_median':ratio,'plans':plans})
        print('Both choices use the same 56-plan universe: 28 layouts × a uniform word/bytes family. '
              'No per-operation hybrid was measured. Preparation is outside this repeated-use trace. '
              'Report observed minima without claiming statistical separation of near ties.')
if args.json_output:
    args.json_output.parent.mkdir(parents=True,exist_ok=True)
    args.json_output.write_text(json.dumps(all_results,indent=2)+'\n')
