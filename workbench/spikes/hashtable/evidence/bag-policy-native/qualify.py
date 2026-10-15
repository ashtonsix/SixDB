#!/usr/bin/env python3
"""Replay captured reports and bind the complete policy panel to native audits."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(__doc__)
p.add_argument('raw', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--codegen', type=Path)
p.add_argument('--verify-binaries', action='store_true')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
here = Path(__file__).parent
processes = json.loads((a.raw/'processes.json').read_text())
assert processes['bag_policies'] and processes['widths'] == [2,8,16]
assert processes['passes'] == 2 and processes['reps'] == 5
assert len(processes['processes']) == 1000
for row in processes['processes']:
    assert not row['stderr'] and row['returncode'] == 0
    assert Path(row['argv'][3]).name in ['values-2','values-8','values-16']

# Original argv retain remote paths. Verify recovered ELFs by basename without
# changing that receipt or rewriting the frozen report's input.
for w in [2,8,16]:
    if a.verify_binaries:
        binary = a.raw/f'values-{w}'
        assert hashlib.sha256(binary.read_bytes()).hexdigest() == processes['binary_sha256'][str(w)]

for report, name in [('bag-policy-report.py','summary.json'),
                     ('bag-events-report.py','events-summary.json')]:
    subprocess.run([sys.executable, str(here/report), str(a.raw),
                    '--output', str(a.output/name)], check=True, stdout=subprocess.DEVNULL)
summary = json.loads((a.output/'summary.json').read_text())
if (a.raw/'summary.json').exists():
    assert summary == json.loads((a.raw/'summary.json').read_text())
assert (a.output/'summary.csv').read_bytes() == (a.raw/'summary.csv').read_bytes()
assert summary['rows'] == 45000 and summary['processes'] == 1000
assert len(summary['comparison']) == 4500

events = json.loads((a.output/'events-summary.json').read_text())
archived_events = json.loads((a.raw/'events-summary.json').read_text())
assert events['matching_timed_rows'] == 270
assert len(events['phases']) == 27 and events['contexts'] == 9
# Binary fingerprints are optional in the original report when regenerating
# solely from compact retained files. Compare every other field unchanged.
for field in events:
    if field != 'sha256':
        assert events[field] == archived_events[field], field
for name, digest in events['sha256'].items():
    assert archived_events['sha256'][name] == digest, name
if a.verify_binaries:
    assert events == archived_events

job = json.loads((a.raw/'job.json').read_text())
audits = {}
for w in [2,8,16]:
    path = (a.codegen/f'audit-{w}'/'codegen-summary.json' if a.codegen else
            a.raw/f'codegen-W{w}.json')
    audit = json.loads(path.read_text())
    assert audit['panel'] == 'bag_policy' and audit['qualified']
    assert (audit['timed_regions'],audit['read_regions'],audit['update_regions']) == (80,60,20)
    assert not audit['update_callgraph_truncated']
    assert audit['value_bytes'] == w and audit['key_bits'] == 40
    assert audit['binary_sha256'] == processes['binary_sha256'][str(w)]
    audits[str(w)] = audit
assert len({x['source_sha256'] for x in audits.values()}) == 1
reports = {name:hashlib.sha256((here/name).read_bytes()).hexdigest()
           for name in ['bag-policy-report.py','bag-events-report.py']}
coverage = dict(rows=45000,processes=1000,complete_case_comparisons=4500,
                passes=2,repetitions_per_process=5,observer_histories=9,
                observer_phases=27,matching_timed_rows=270,
                read_regions=180,update_regions=60,
                source_manifest_digest=job['source']['digest'],
                source_archive_sha256=job['source']['sha256'],
                source_driver_sha256=next(iter(audits.values()))['source_sha256'],
                report_sha256=reports,timed_elf_sha256=processes['binary_sha256'],
                checks=['complete seeded schedule, forward/reverse order and phase inventories',
                        'serialized process timestamps, zero return codes and empty stderr',
                        'all comparisons reproduced, including losses and each pass median',
                        'all 27 observer inventories equal matching timed phase inventories',
                        'native audited ELF identities equal actual timed process identities'],
                limitations=['Logical observer counts are untimed; issued damage overlaps and is not traffic or MVCC copying.',
                             'Static CFG recurrence is not dynamic call frequency or timing cost.',
                             'Repeated processes on one host are not independent host trials.',
                             'Policies change geometry, inline capacity and/or spill ownership; no single-factor attribution is implied.',
                             'Measured widths are 2/8/16 bytes, K40 multimaps only; bytes do not establish cache residence.'])
(a.output/'coverage.json').write_text(json.dumps(coverage,indent=2)+'\n')
print('Qualified 45,000 rows / 1,000 processes; 270 observer matches; three timed ELF audits')
