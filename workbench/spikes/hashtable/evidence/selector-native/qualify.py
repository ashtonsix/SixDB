#!/usr/bin/env python3
"""Replay the complete frozen selector report and bind all native ELF audits."""
import argparse
from collections import defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

p=argparse.ArgumentParser(__doc__)
p.add_argument('raw',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--codegen',type=Path)
p.add_argument('--verify-binaries',action='store_true')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
here=Path(__file__).parent
receipt=json.loads((a.raw/'processes.json').read_text())
assert receipt['keys']==[16,40] and receipt['widths']==[2]
assert receipt['passes']==2 and receipt['reps']==5
variants=['composed16','direct16','composed32','direct32']
assert receipt['variants']==variants
assert len(receipt['processes'])==576
for process in receipt['processes']:
    assert process['returncode']==0 and not process['stderr']
    assert Path(process['argv'][3]).name.startswith('selector-')

report=here/'geometry-report.py'
command=[sys.executable,str(report),str(a.raw),'--output',str(a.output/'summary.json')]
if a.verify_binaries:command.append('--verify-binaries')
subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
summary=json.loads((a.output/'summary.json').read_text())
if (a.raw/'summary.json').exists():
    assert summary==json.loads((a.raw/'summary.json').read_text())
assert (a.output/'summary.csv').read_bytes()==(a.raw/'summary.csv').read_bytes()
assert (summary['rows'],summary['processes'],len(summary['comparison']))==(25920,576,2592)
assert all(r['phase']!='serial' for r in summary['comparison'] if r['value_bytes']==0)
assert all(r['phase']!='ordered' for r in summary['comparison'] if r['value_bytes']!=0)

# Qualify actual inventories, not only median logical-byte totals. Execution
# substitution must preserve these complete geometry/history controls.
inventory_fields=['requested','owned_capacity','metadata','A','B','n','bucket_bytes',
                  'primary_slots','secondary_slots','primary_used','primary_padding',
                  'secondary_used','secondary_padding','build_primary_rows',
                  'build_secondary_rows','build_overflow_rows']
inventories=defaultdict(lambda:defaultdict(set))
for row in csv.DictReader((a.raw/'values.csv').open()):
    assert math.isfinite(float(row['ns'])) and float(row['ns'])>0
    key=tuple(row[f] for f in ['K','value_bytes','reference_A','arm','phase'])
    inventories[key][row['selector_variant']].add(tuple(int(row[f]) for f in inventory_fields))
for key,by_variant in inventories.items():
    assert set(by_variant)==set(variants)
    assert all(len(v)==1 for v in by_variant.values()),('varying phase inventory',key)
    assert len({next(iter(v)) for v in by_variant.values()})==1,('variant inventory mismatch',key)

audits={}
for variant in variants:
    for k in receipt['keys']:
        w=2;key=f'{variant}-{k}-{w}'
        path=(a.codegen/f'audit-{key}'/'codegen-summary.json' if a.codegen else a.raw/f'codegen-{key}.json')
        audit=json.loads(path.read_text())
        assert audit['panel']=='geometry' and audit['qualified']
        assert audit['selector_variant']==variant
        assert audit['selector_controls']=={'HT_VALUE_DIRECT':int(variant.startswith('direct')),
                                           'HT_RECIPE_SCAN32':int(variant.endswith('32'))}
        assert (audit['timed_regions'],audit['read_regions'],audit['update_regions'])==(144,108,36)
        assert not audit['update_callgraph_truncated']
        assert (audit['key_bits'],audit['value_bytes'])==(k,w)
        assert audit['binary_sha256']==receipt['binary_sha256'][key]
        assert len(audit['geometries'])==16
        assert 'ordered set traversal' not in audit['phase_semantics']
        audits[key]=audit
assert len({r['source_sha256'] for r in audits.values()})==1
job=json.loads((a.raw/'job.json').read_text())
coverage=dict(rows=25920,processes=576,complete_case_comparisons=2592,
              passes=2,repetitions_per_process=5,elf_count=8,
              timed_regions=1152,read_regions=864,update_regions=288,
              ordered_set_regions=0,map_serial_regions=288,selector_variants=variants,
              source_manifest_digest=job['source']['digest'],
              source_archive_sha256=job['source']['sha256'],
              source_driver_sha256=next(iter(audits.values()))['source_sha256'],
              report_sha256=hashlib.sha256(report.read_bytes()).hexdigest(),
              timed_elf_sha256=receipt['binary_sha256'],
              checks=['complete seeded schedule, forward/reverse order and all phase operation counts',
                      'serialized process timestamps, zero returns and empty stderr',
                      'complete report reproduced exactly, including losses and per-pass medians',
                      'every phase requested/owned/metadata inventory and geometry/population agrees across variants',
                      'independent fitting capacities, padding, tier populations and reference/actual geometry',
                      'all eight native audited ELF identities equal timed process identities'],
              limitations=['K16/K40 W2 maps only; no set, multimap, other-width or growth-path performance is qualified.',
                           'Map serial is the driver label. Call fusion alone does not prove complete value-byte dataflow or a machine-code dependency chain.',
                           'Static CFG recurrence is not dynamic call frequency or timing cost.',
                           'Variant comparisons keep geometry and logical representation equal. Code generation changes beyond selector instructions, so whole-operation differences are not isolated selector cost.',
                           'Repeated processes on one host are not independent host trials; unchanged Abseil source can still vary with placement or runtime seed.',
                           'Build tier populations and phase memory inventories do not measure transient peaks or establish cache residence.',
                           'Hash count-register feedback must be checked before attributing changes to selector width. Primitive-only codegen evidence does not establish full-table latency or throughput.'])
(a.output/'coverage.json').write_text(json.dumps(coverage,indent=2)+'\n')
print('Qualified 25,920 rows / 576 processes / 2,592 comparisons; eight timed ELF audits')
