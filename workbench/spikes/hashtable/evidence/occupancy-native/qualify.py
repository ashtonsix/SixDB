#!/usr/bin/env python3
"""Replay the complete frozen occupancy-v2 report and bind all native ELF audits."""
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
assert a.raw.resolve()!=a.output.resolve(),'replay output must be separate from raw evidence'
here=Path(__file__).parent
cohort=json.loads((here/'cohort.json').read_text())
receipt=json.loads((a.raw/'processes.json').read_text())
cases=['16_2_1','40_0_0','40_2_1','40_8_2','64_16_1'];encodings=['count','inactive']
assert receipt['cases']==cases and receipt['arms']==list(range(18))
assert (receipt['passes'],receipt['reps'])==(2,5)
assert len(receipt['processes'])==720
for process in receipt['processes']:
    assert process['returncode']==0 and not process['stderr']
    assert Path(process['argv'][3]).name.startswith('occupancy-')
# The unchanged captured reporter writes into its input directory. Link only
# immutable raw inputs into a separate replay directory, never rewrite evidence.
for name in ['processes.json','values.csv']:
    link=a.output/name
    if link.exists() or link.is_symlink():assert link.resolve()==(a.raw/name).resolve()
    else:link.symlink_to((a.raw/name).resolve())
if a.verify_binaries:
    for case in cases:
        for encoding in encodings:
            name=f'occupancy-{case}-{encoding}';link=a.output/name
            if not link.exists():link.symlink_to((a.raw/name).resolve())
report=here/'occupancy-run.py'
assert hashlib.sha256(report.read_bytes()).hexdigest()==cohort['source_sha256']['occupancy-run.py']
command=[sys.executable,str(report),'report',str(a.output)]
if a.verify_binaries:command.append('--verify-binaries')
subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
summary=json.loads((a.output/'summary.json').read_text())
if (a.raw/'summary.json').exists():assert summary==json.loads((a.raw/'summary.json').read_text())
assert (a.output/'summary.csv').read_bytes()==(a.raw/'summary.csv').read_bytes()
assert (summary['rows'],summary['processes'],len(summary['comparison']))==(46800,720,4680)
# Complete phase inventories remain paired, not just build totals. Build
# populations are historical labels; they do not describe the aged table.
fields=['requested','owned_capacity','metadata','A','B','n','bucket_bytes',
        'primary_slots','secondary_slots','primary_used','primary_padding',
        'secondary_used','secondary_padding','build_primary_rows',
        'build_secondary_rows','build_overflow_rows']
inventories=defaultdict(lambda:defaultdict(list))
for row in csv.DictReader((a.raw/'values.csv').open()):
    assert math.isfinite(float(row['ns'])) and float(row['ns'])>=0
    assert (row['phase']!='serial' if row['kind']=='0' else row['phase']!='ordered')
    key=tuple(row[f] for f in ['K','value_bytes','kind','reference_A','arm','phase'])
    inventories[key][row['encoding']].append(tuple(int(row[f]) for f in fields))
for key,by_encoding in inventories.items():
    assert set(by_encoding)==set(encodings)
    assert sorted(by_encoding['count'])==sorted(by_encoding['inactive']),('phase inventory mismatch',key)
audits={}
for case in cases:
    k,w,kind=map(int,case.split('_'))
    for encoding in encodings:
        key=f'{case}-{encoding}'
        path=a.codegen/f'audit-{key}'/'codegen-summary.json' if a.codegen else a.raw/f'codegen-{key}.json'
        audit=json.loads(path.read_text())
        assert audit['panel']=='geometry' and audit['qualified']
        assert audit['occupancy_variant']==encoding and audit['occupancy_source_version']=='v2'
        assert 'selector_variant' not in audit and 'mask_variant' not in audit
        assert audit['occupancy_controls']=={'HT_OCCUPANCY_STUDY':1,'HT_GEOMETRY_KIND':kind,
            'HT_RECIPE_SCAN32':1,'HT_HASH_MASK':2,'HT_BUCKET_SENTINEL':int(encoding=='inactive')}
        assert (audit['timed_regions'],audit['read_regions'],audit['update_regions'])==(252,216,36)
        assert not audit['update_callgraph_truncated']
        assert (audit['key_bits'],audit['value_bytes'],audit['container_kind'])==(k,w,kind)
        assert audit['binary_sha256']==receipt['binary_sha256'][f'occupancy-{key}']
        assert audit['source_sha256']==cohort['source_sha256']['value_width_bench.cpp']
        assert len(audit['geometries'])==16
        assert ('ordered set traversal' in audit['phase_semantics'])==(kind==0)
        audits[key]=audit
job=json.loads((a.raw/'job.json').read_text())
assert job['source']['digest']==cohort['capture']['source_digest']
coverage=dict(rows=46800,processes=720,complete_case_comparisons=4680,
    passes=2,repetitions_per_process=5,elf_count=10,timed_regions=2520,read_regions=2160,update_regions=360,
    ordered_set_regions=72,map_serial_regions=216,multimap_serial_regions=72,
    encodings=encodings,cases=cases,source_version='v2',
    source_manifest_digest=job['source']['digest'],source_archive_sha256=job['source']['sha256'],
    source_driver_sha256=cohort['source_sha256']['value_width_bench.cpp'],
    report_sha256=hashlib.sha256(report.read_bytes()).hexdigest(),timed_elf_sha256=receipt['binary_sha256'],
    checks=['complete seeded schedule, forward/reverse process order and all13 phase operation counts',
        'serialized process timestamps, zero returns and empty stderr',
        'complete frozen report reproduces exactly, including losses and per-pass medians',
        'every phase requested/owned/metadata inventory and geometry agrees across encodings',
        'independent fitting capacities, padding, build populations and reference/actual geometry',
        'all ten native audited ELF identities equal timed process identities',
        'six read phases and update resolve through actual frozen-source DWARF callsites; reuse timer recognized but not mutation-audited'],
    limitations=['Five domain/value/kind cases only; not full API/domain/width/growth or DRAM coverage.',
        'Cancelled v1 is excluded; revised post-churn point oracle and construction snapshot are from reviewed v2.',
        'Map/MM serial is the driver label. Call fusion alone does not prove full value-byte dataflow or the actual machine-code dependency chain.',
        'Ordered set traversal is not strict serial lookup latency.',
        'MM starts singleton, updates to pairs, deletes half and reuses those keys as singletons. Reused reads cover a half-pair/half-singleton history, not original uniform fanout.',
        'Build tier populations remain build measurements on every row; they do not report post-deletion or post-reuse populations.',
        'Phase inventories exclude transient peaks; table bytes do not establish cache residence.',
        'Static CFG recurrence is not dynamic call frequency or cost. Conditional cold allocation and spill calls remain reported.',
        'Repeated processes on one host are not independent host trials. Unchanged Abseil source can vary with code placement/runtime seed.',
        'Encoding changes keep inventories matched but alter complete generated code; timing differences are not isolated instruction costs. No preset or promotion follows from this audit.'])
(a.output/'coverage.json').write_text(json.dumps(coverage,indent=2)+'\n')
print('Qualified 46,800 rows / 720 processes / 4,680 comparisons; ten timed ELF audits')
