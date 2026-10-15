#!/usr/bin/env python3
"""Replay the complete frozen geometry report and bind all native ELF audits."""
import argparse
import hashlib
import json
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
assert receipt['keys']==[16,40,64] and receipt['widths']==[0,2,8,16]
assert receipt['passes']==2 and receipt['reps']==5
assert len(receipt['processes'])==864
for process in receipt['processes']:
    assert process['returncode']==0 and not process['stderr']
    assert Path(process['argv'][3]).name.startswith('geometry-')

report=here/'geometry-report.py'
command=[sys.executable,str(report),str(a.raw),'--output',str(a.output/'summary.json')]
if a.verify_binaries:command.append('--verify-binaries')
subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
summary=json.loads((a.output/'summary.json').read_text())
if (a.raw/'summary.json').exists():
    assert summary==json.loads((a.raw/'summary.json').read_text())
assert (a.output/'summary.csv').read_bytes()==(a.raw/'summary.csv').read_bytes()
assert (summary['rows'],summary['processes'],len(summary['comparison']))==(38880,864,3888)
assert all(r['phase']!='serial' for r in summary['comparison'] if r['value_bytes']==0)
assert all(r['phase']!='ordered' for r in summary['comparison'] if r['value_bytes']!=0)

audits={}
for k in receipt['keys']:
    for w in receipt['widths']:
        key=f'{k}-{w}'
        path=(a.codegen/f'audit-{key}'/'codegen-summary.json' if a.codegen else a.raw/f'codegen-{key}.json')
        audit=json.loads(path.read_text())
        assert audit['panel']=='geometry' and audit['qualified']
        assert 'selector_variant' not in audit,'selector controls are a separate cohort'
        assert (audit['timed_regions'],audit['read_regions'],audit['update_regions'])==(144,108,36)
        assert not audit['update_callgraph_truncated']
        assert (audit['key_bits'],audit['value_bytes'])==(k,w)
        assert audit['binary_sha256']==receipt['binary_sha256'][key]
        assert len(audit['geometries'])==16
        assert ('ordered set traversal' in audit['phase_semantics'])==(w==0)
        audits[key]=audit
assert len({r['source_sha256'] for r in audits.values()})==1
job=json.loads((a.raw/'job.json').read_text())
coverage=dict(rows=38880,processes=864,complete_case_comparisons=3888,
              passes=2,repetitions_per_process=5,elf_count=12,
              timed_regions=1728,read_regions=1296,update_regions=432,
              ordered_set_regions=108,map_serial_regions=324,
              source_manifest_digest=job['source']['digest'],
              source_archive_sha256=job['source']['sha256'],
              source_driver_sha256=next(iter(audits.values()))['source_sha256'],
              report_sha256=hashlib.sha256(report.read_bytes()).hexdigest(),
              timed_elf_sha256=receipt['binary_sha256'],
              checks=['complete seeded schedule, forward/reverse order and all phase operation counts',
                      'serialized process timestamps, zero returns and empty stderr',
                      'complete report reproduced exactly, including losses and per-pass medians',
                      'independent fitting capacities, padding, tier populations and reference/actual geometry',
                      'all twelve native audited ELF identities equal timed process identities'],
              limitations=['Ordered set reads are not strict serial lookup latency; branch-synthesized Boolean results invalidate that interpretation.',
                           'Map serial is the driver label. Call fusion alone does not prove complete value-byte dataflow or a machine-code dependency chain.',
                           'Static CFG recurrence is not dynamic call frequency or timing cost.',
                           'Geometry controls change primary and secondary organization jointly, at fixed input cardinality.',
                           'Repeated processes on one host are not independent host trials; unchanged Abseil source can still vary with placement or runtime seed.',
                           'Build tier populations and phase memory inventories do not measure transient peaks or establish cache residence.',
                           'K16/K40/K64 sets and W2/W8/W16 maps only; no preset, growth policy, or broad-domain conclusion follows.'])
(a.output/'coverage.json').write_text(json.dumps(coverage,indent=2)+'\n')
print('Qualified 38,880 rows / 864 processes / 3,888 comparisons; twelve timed ELF audits')
