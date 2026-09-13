#!/usr/bin/env python3
"""Offline shortlist and held-out-mixture experiments over retained complete plans."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
from statistics import median
import sys
import time

STUDY = Path(__file__).resolve().parent.parent
ROOT = STUDY.parents[2]
original_import_path = sys.path.copy()
sys.path.insert(0, str(STUDY / 'tuplepack-reference'))
import analyse as reference
sys.path[:] = original_import_path
sys.path.insert(0, str(ROOT / 'workbench/spikes/tuple-layout/runtime'))
from report import read, verify_compact
sys.path[:] = original_import_path


def load(root):
    verify_compact(root)
    results = []
    for path in sorted(root.glob('*/samples.csv')):
        times = read(path)
        weights = {}
        for row in csv.DictReader(path.open()):
            if row['name'].startswith('mixture/'):
                mix = int(row['name'].split('/')[2])
                observed = {k.removeprefix('fraction_'): float(v)
                            for k, v in row.items() if k.startswith('fraction_') and v}
                assert mix not in weights or weights[mix] == observed
                weights[mix] = observed
        for mix, observed in sorted(weights.items()):
            plans = {}
            for name, actual in times.items():
                parts = name.split('/')
                if len(parts) != 4 or parts[0] != 'mixture' or int(parts[2]) != mix:
                    continue
                _, candidate, _, family = parts
                predicted = sum(w * times[f'small/{candidate}/{op}/{family}']
                                for op, w in observed.items())
                assert actual > 0 and predicted > 0
                plans[candidate, family] = (actual, predicted)
            assert len(plans) == 56
            results.append(dict(context=f'{root.name}/{path.parent.name}', mix=mix,
                                weights=observed, plans=plans, source=path))
    return results


def diversity(layouts):
    vectors = {l.id: reference.diversity_vector(l) for l in layouts}
    ranges = [max(v[i] for v in vectors.values()) - min(v[i] for v in vectors.values())
              for i in range(len(next(iter(vectors.values()))))]
    def distance(a, b):
        return sum(abs(x-y)/r for x,y,r in zip(vectors[a], vectors[b], ranges) if r)
    selected = [min(vectors)]
    while len(selected) < len(vectors):
        remaining = set(vectors)-set(selected)
        selected.append(min(remaining, key=lambda x: (-min(distance(x,y) for y in selected), x)))
    return selected


def score(plans, choices):
    return min(choices, key=lambda p: (plans[p][0], p))


def run(cases):
    layouts = {l.id: l for l in reference.layouts()}
    shortlists, palettes = [], []
    for case in cases:
        plans = case['plans']
        ids = sorted({p[0] for p in plans})
        baseline = score(plans, plans)
        optimum = plans[baseline][0]
        rankings = {key: sorted(ids, key=lambda c: (reference.heuristic_keys(layouts[c], case['weights'])[key], c))
                    for key in ('density_first','edge_first','outer_order_first','coaccess_first')}
        rankings['diverse'] = diversity([layouts[c] for c in ids])
        rankings['isolated'] = sorted(ids, key=lambda c: (min(plans[c,f][1] for f in ('word','bytes')), c))
        for method, order in rankings.items():
            for budget in (1,2,4,8,16,28):
                candidates = order[:budget]
                chosen = score(plans, [(c,f) for c in candidates for f in ('word','bytes')])
                shortlists.append(dict(context=case['context'], mix=case['mix'], method=method,
                    layout_budget=budget, complete_plan_probes=2*budget,
                    candidates=candidates, chosen=list(chosen), actual_ns=plans[chosen][0],
                    optimum_ns=optimum, regret=plans[chosen][0]/optimum-1))

        # Held-out workload classes from the same capture, not independent data containers.
        train = [c for c in cases if c['context']==case['context'] and c['mix']!=case['mix']]
        assert len(train)==2 and all(set(c['plans'])==set(plans) for c in train)
        train_optima = [min(v[0] for v in c['plans'].values()) for c in train]
        selected = []
        for size in range(1,9):
            def objective(candidate):
                pool = selected + [candidate]
                return sum(min(c['plans'][p][0] for p in pool)/opt
                           for c,opt in zip(train,train_optima)) / len(train)
            chosen = min(set(plans)-set(selected), key=lambda p: (objective(p),p))
            selected.append(chosen)
            if size not in (1,2,4,8):
                continue
            # Fast selection borrows the nearest *training* mixture's measured ranking.
            nearest = min(train,key=lambda c: (abs(c['mix']-case['mix']),c['mix']))
            fast = score(nearest['plans'],selected)
            # Probing each retained plan in the new context is local empirical fitting.
            fitted = score(plans,selected)
            palettes.append(dict(context=case['context'], held_out_mix=case['mix'],
                training_mixes=[c['mix'] for c in train], palette_size=size,
                acquisition_plan_observations=sum(len(c['plans']) for c in train),
                palette=[list(p) for p in selected], fast_choice=list(fast),
                fast_regret=plans[fast][0]/optimum-1,
                fitted_choice=list(fitted), fitted_regret=plans[fitted][0]/optimum-1,
                local_plan_probes=size, optimum_ns=optimum))
    return shortlists,palettes


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--evidence',type=Path,nargs='+',default=[
        ROOT/'workbench/spikes/tuple-layout/evidence/final-v2',
        ROOT/'workbench/spikes/tuple-layout/evidence/final-zen5'])
    args=ap.parse_args()
    cases=[case for path in args.evidence for case in load(path)]
    start=time.perf_counter()
    shortlists,palettes=run(cases)
    elapsed=time.perf_counter()-start
    args.output.mkdir(parents=True,exist_ok=True)
    provenance={'kind':'derived retained measurements','analysis_seconds':elapsed,
        'analysis_timing_scope':'one Python analysis run; not a production selector benchmark',
        'code_hashes':{str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [Path(__file__),STUDY/'tuplepack-reference/analyse.py',
                         ROOT/'workbench/spikes/tuple-layout/runtime/report.py']},
        'input_hashes':{str(c['source'].relative_to(ROOT)):hashlib.sha256(c['source'].read_bytes()).hexdigest() for c in cases},
        'limits':['28-layout measured subset only; structural diversity restricted to that subset',
          'finalist and local-fit choices charge plan-probe counts, not estimated experiment time',
          'held-out workload classes share a capture and data set; not collection/key-region/temporal holdouts',
          'three observed repetitions summarized by medians; no statistical winner claim',
          'no independent container generalization or migration performance established']}
    for filename,data in [('shortlists.json',shortlists),('palettes.json',palettes),('provenance.json',provenance)]:
        (args.output/filename).write_text(json.dumps(data,indent=2)+'\n')
    lines=['# Retained-plan selection study','',
        'Each cell is observed subset regret after complete-plan probes. Median / maximum across the nine machine/mixture cases; no statistical winner claim.','',
        '| Method | 1 layout | 2 | 4 | 8 | 16 | 28 |','| --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    for method in sorted({r['method'] for r in shortlists}):
        cells=[]
        for b in (1,2,4,8,16,28):
            v=[100*r['regret'] for r in shortlists if r['method']==method and r['layout_budget']==b]
            cells.append(f'{median(v):.2f}% / {max(v):.2f}%')
        lines.append('| '+method+' | '+' | '.join(cells)+' |')
    lines += ['','Held-out mixture selection: two other mixtures train each palette. Fast uses nearest training mixture; fitted probes every palette plan.','',
        '| Palette plans | Fast median / max regret | Fitted median / max regret |',
        '| ---: | ---: | ---: |']
    for size in (1,2,4,8):
        subset=[r for r in palettes if r['palette_size']==size]
        cells=[]
        for key in ('fast_regret','fitted_regret'):
            v=[100*r[key] for r in subset]
            cells.append(f'{median(v):.2f}% / {max(v):.2f}%')
        lines.append(f'| {size} | '+' | '.join(cells)+' |')
    lines+=['','## Limits','']+['- '+s for s in provenance['limits']]
    (args.output/'summary.md').write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines))


if __name__=='__main__':
    main()
