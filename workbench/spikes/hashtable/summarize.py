#!/usr/bin/env python3
"""Summarize captured bucket probes, retaining all recipes and sample variation."""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import statistics

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('inputs', nargs='+', help='machine=worker-results-directory')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
rows, receipts = [], []
for item in a.inputs:
    machine, directory = item.split('=',1)
    directory = Path(directory)
    complete = json.loads((directory/'worker-result.json').read_text())
    if complete['state'] != 'complete' or complete['script_returncode']:
        raise RuntimeError('incomplete worker: ' + str(directory))
    files = sorted(directory.glob('base-*.csv')) + sorted(directory.glob('wide-*.csv'))
    if not files:
        raise RuntimeError('no samples: ' + str(directory))
    for file in files:
        isa = file.name.split('-')[0]
        groups = collections.defaultdict(list)
        keys = ['fingerprint_bits','suffix_bits','buckets','payload_bytes','plane_predicate','tuple_recipe','serial']
        with file.open() as f:
            for row in csv.DictReader(f):
                if 'raw_tuple' in row:
                    row['tuple_recipe'] = row.pop('raw_tuple')
                groups[tuple(row[k] for k in keys)].append(row)
        for key,samples in sorted(groups.items()):
            if len(samples) != 5 or {r['rep'] for r in samples} != {'0','1','2','3','4'}:
                raise RuntimeError('unexpected repetitions: ' + str(file))
            if len({r['result'] for r in samples}) != 1:
                raise RuntimeError('unstable result: ' + str(file))
            times = [float(r['ns_per_op']) for r in samples]
            rows.append({'machine':machine,'isa':isa, **dict(zip(keys,key)),
                         'median_ns':statistics.median(times),'min_ns':min(times),'max_ns':max(times),
                         'repetitions':len(times)})
    receipts.append({'machine':machine,'job':directory.parent.name,
        'artifact':json.loads((directory.parent/'artifact.json').read_text()),
        'sample_hashes':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in files},
        'source_hashes':(directory/'sources.sha256').read_text(),
        'hardware_sha256':hashlib.sha256((directory/'hardware.txt').read_bytes()).hexdigest(),
        'checks':{f.name:f.read_text() for f in directory.glob('*-check.txt')}})
a.output.mkdir(parents=True,exist_ok=True)
with (a.output/'bucket-summary.csv').open('w') as f:
    writer=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n')
    writer.writeheader();writer.writerows(rows)
(a.output/'bucket-receipts.json').write_text(json.dumps({
    'scope':'full16-slot warmed hit-only bucket consumer; no complete-table, residence, memory-advantage or Abseil claim',
    'tuple_recipes':{'0':'generic TuplePack body','1':'fixed raw-load control','2':'admitted existing TuplePack transfer body'},
    'receipts':receipts},indent=2)+'\n')
print(json.dumps({'rows':len(rows),'output':str(a.output)}))
