#!/usr/bin/env python3
"""Regenerate the dirty-buffer comparison from raw repetitions and accounting."""
import csv
from pathlib import Path
import re
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from evidence import read_measurements

def main():
    root=Path(sys.argv[1])
    receipt,benchmark=read_measurements(root)
    if receipt['status'] not in ('complete','running'): raise ValueError('failed receipt')
    config=receipt['config']
    accounting=list(csv.DictReader((root/'accounting.csv').open()))
    expected={f'{r["case"]}/t{r["threads"]}/{r["method"]}':r for r in accounting if re.search(config['filter'],f'{r["case"]}/t{r["threads"]}/{r["method"]}/manual_time')}
    if not expected: raise ValueError('empty selection')
    groups={k:[] for k in expected}
    for sample in benchmark['benchmarks']:
        if sample.get('run_type')!='iteration': continue
        if sample.get('error_occurred'): raise ValueError(sample)
        label='/'.join(sample['name'].split('/')[:3])
        groups[label].append(sample)
    output=[]
    for label,samples in groups.items():
        if len(samples)!=config['repetitions']: raise ValueError(f'{label}: incomplete repetitions')
        a=expected[label]
        if a['correct']!='1': raise ValueError('oracle mismatch')
        values=[s['real_time']/s['updates'] for s in samples]
        median=statistics.median(values)
        out=dict(a)
        out.update(ns_per_update=median,spread_percent=100*(max(values)-min(values))/median)
        for phase in ('write','read','flush','drain','reset'):
            out[phase+'_ns']=statistics.median(s.get(phase+'_ns_per_update',0) for s in samples)
        output.append(out)
    with (root/'summary.csv').open('w') as f:
        writer=csv.DictWriter(f,fieldnames=list(output[0]),lineterminator='\n'); writer.writeheader(); writer.writerows(output)
    text=['# Shared dirty-buffer measurements','','Manual full-cycle ns/update: writes + batch-boundary queries + full drain/reset.','Independent phase medians need not sum to the cycle median. ARM-VM affinity is not proof of physical-core placement.','','| Case | Writers | Method | Cycle ns/update | Write | Read | Replay | Reset | Spread |','| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    for r in output:
        text.append(f'| {r["case"]} | {r["threads"]} | {r["method"]} | {r["ns_per_update"]:.2f} | {r["write_ns"]:.2f} | {r["read_ns"]:.2f} | {r["drain_ns"]:.2f} | {r["reset_ns"]:.2f} | {r["spread_percent"]:.1f}% |')
    (root/'summary.md').write_text('\n'.join(text)+'\n')
    print(f'{len(output)} comparisons, all repetitions and correctness records present.')
if __name__=='__main__': main()
