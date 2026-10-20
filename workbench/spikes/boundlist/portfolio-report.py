#!/usr/bin/env python3
"""Export final portfolio cells; all losing cells remain alongside the wins.

Recover the three worker bundles first with workbench/tools/worker.py fetch JOB.
Run from any directory; --platform can export one completed worker while others
run. Timings are medians of raw repetitions, with range/sample count retained.
No samples are deleted and no different source cohort is pooled.
"""
import argparse
import csv
import json
import re
import statistics
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--platform', choices=('zen5','granite','neon'), action='append')
args = parser.parse_args()
inputs = json.loads((HERE/'portfolio-inputs.json').read_text())
out = HERE/'evidence/portfolio'
out.mkdir(parents=True, exist_ok=True)
receipts = {k:v for k,v in inputs.items() if k != 'panels'}
receipts['workers'] = {}
for panel, pattern in inputs['panels'].items():
    groups = defaultdict(list)
    dimensions = None
    measures = None
    jobs = inputs.get('mutation_jobs' if panel == 'mutation' else 'edit_jobs' if panel == 'edits' else 'jobs', inputs['jobs'])
    for platform, job in jobs.items():
        if args.platform and platform not in args.platform:
            continue
        source = ROOT/'build/workers'/job/'results'
        outcome = json.loads((source/'worker-result.json').read_text())
        assert outcome['state'] == 'complete' and outcome['script_returncode'] == 0, job
        record = json.loads((source/'job.json').read_text())
        expected = inputs['mutation_capture'] if panel in ('mutation','edits') else inputs['source_capture']
        assert record['source']['digest'] == expected, (job,expected)
        receipts['workers'][job] = {k:record[k] for k in ('id','source','source_commit','script','uri','worker')}
        receipts['workers'][job]['platform'] = platform
        receipts['workers'][job]['result'] = outcome
        files = sorted(source.glob(pattern))
        if panel == 'reads':
            files = [f for f in files if re.search(r'-p[0-9]+-t', f.name)]
        assert files, (platform,panel,source)
        for file in files:
            parts = file.name.split('-')
            isa = parts[0]
            variant = parts[1] if parts[1] in ('before','after') else 'after'
            with file.open() as stream:
                reader = csv.DictReader(stream)
                numeric = [x for x in reader.fieldnames if x in ('ns','setup_ns','close_ns') or x.startswith('ns_')]
                extras = [x for x in reader.fieldnames if x.endswith('_bytes') or x.endswith('_backing') or x == 'ring_visits']
                fields = [x for x in reader.fieldnames if x not in numeric+extras+['rep','checksum']]
                if dimensions is None:
                    dimensions = ['platform','isa','variant']+fields
                    measures = numeric+extras
                assert dimensions[3:] == fields and measures == numeric+extras, file
                for row in reader:
                    if panel == 'cursor-diagnostic' and any(row[k] != v for k,v in dict(maximum='4095',used='1',placement='0',concurrency='1',operation='construct').items()):
                        continue
                    key = (platform,isa,variant)+tuple(row[x] for x in fields)
                    groups[key].append({x:float(row[x]) for x in measures})
        if (source/'host.json').exists():
            receipts['workers'][job]['host'] = json.loads((source/'host.json').read_text())
    with (out/f'{panel}.csv').open('w') as stream:
        fields = dimensions+['samples']+[f'{m}_{stat}' for m in measures for stat in ('median','min','max')]
        writer = csv.DictWriter(stream,fieldnames=fields);writer.writeheader()
        for key, rows in sorted(groups.items()):
            row = dict(zip(dimensions,key));row['samples'] = len(rows)
            for m in measures:
                values = [r[m] for r in rows]
                for stat, value in [('median',statistics.median(values)),('min',min(values)),('max',max(values))]:
                    row[f'{m}_{stat}'] = f'{value:.6f}' if 'ns' in m else f'{value:g}'
            writer.writerow(row)
    print(panel,len(groups),'cells;',sum(map(len,groups.values())),'samples')
(out/'provenance.json').write_text(json.dumps(receipts,indent=2)+'\n')
