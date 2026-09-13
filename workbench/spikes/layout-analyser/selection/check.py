#!/usr/bin/env python3
"""Check finite-space coverage and that held-out costs cannot choose a palette."""
import copy
import study

cases=[case for root in ['final-v2','final-zen5'] for case in
       study.load(study.ROOT/'workbench/spikes/tuple-layout/evidence'/root)]
shortlists,palettes=study.run(cases)
assert len(cases)==9 and len(shortlists)==9*6*6 and len(palettes)==9*4
for row in shortlists:
    if row['layout_budget']==28:assert row['regret']==0
    assert row['chosen'][0] in row['candidates']
for row in palettes:
    assert row['held_out_mix'] not in row['training_mixes']
    assert row['fast_choice'] in row['palette'] and row['fitted_choice'] in row['palette']
    assert row['fitted_regret']<=row['fast_regret']+1e-12

altered=copy.deepcopy(cases)
target=altered[0]
target['plans']={p:(1000/(i+1),10000+i) for i,p in enumerate(sorted(target['plans']))}
_,changed=study.run(altered)
for before,after in zip(palettes,changed):
    if before['context']==target['context'] and before['held_out_mix']==target['mix']:
        assert before['palette']==after['palette']
        assert before['fast_choice']==after['fast_choice']
print('Nine contexts checked; full subset coverage, admissible choices and held-out-cost isolation passed.')
