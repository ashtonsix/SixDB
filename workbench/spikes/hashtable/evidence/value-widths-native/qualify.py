#!/usr/bin/env python3
"""Reproduce the retained analysis report and verify schedule, ELF identity and fixed controls."""
import argparse,csv,json,random,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(__doc__);p.add_argument('raw',type=Path);p.add_argument('output',type=Path);p.add_argument('--codegen',type=Path);p.add_argument('--repetitions',type=int,default=9);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(Path(__file__).with_name('report.py')),str(a.raw/'values.csv'),'--output',str(a.output/'summary.json')],check=True,stdout=subprocess.DEVNULL)
s=json.loads((a.output/'summary.json').read_text());assert s['rows']==220*9*a.repetitions and s['repetitions']==a.repetitions and s['cases']==220
receipt=json.loads((a.raw/'processes.json').read_text());fans={2:[1,2,4,5,8,9,16,32],8:[1,2,3,4,8,32],16:[1,2,3,8,32]}
schedule=[(w,k,arm,f,large) for w,fs in fans.items() for k in [1,2] for arm in range(5) for f in ([1] if k==1 else fs) for large in [0,1]];random.Random(912).shuffle(schedule)
rows=list(csv.DictReader((a.raw/'values.csv').open()));assert len(receipt['processes'])==len(schedule)
for index,(process,case) in enumerate(zip(receipt['processes'],schedule)):
 w,k,arm,f,large=case;argv=process['argv'];assert argv[:2]==['taskset','-c'] and Path(argv[3]).name==f'values-{w}' and argv[4:]==list(map(str,[k,arm,f,large,a.repetitions]))
 assert process['returncode']==0 and not process['stderr'];assert process['ended_unix']>=process['started_unix']
 if index:assert process['started_unix']>=receipt['processes'][index-1]['ended_unix']
 chunk=rows[index*9*a.repetitions:(index+1)*9*a.repetitions];assert {(int(r['value_bytes']),int(r['kind']),int(r['arm']),int(r['fanout']),int(r['A'])) for r in chunk}=={(w,k,arm,f,10 if large else 6)}
audits={}
for w in fans:
 path=a.codegen/f'W{w}'/'codegen-summary.json' if a.codegen else a.raw/f'codegen-W{w}.json'
 audit=json.loads(path.read_text());assert audit['qualified'] and audit['value_bytes']==w and audit['timed_regions']==60
 assert audit['binary_sha256']==receipt['binary_sha256'][str(w)];audits[str(w)]={k:audit[k] for k in ['binary_sha256','source_sha256','read_locations','timed_regions','outlined_timing_wrappers']}
lookup={(r['value_bytes'],r['kind'],r['A'],r['fanout'],r['arm'],r['phase']):r for r in s['comparison']}
fields=['requested','owned_capacity','metadata','inline_values','abseil_inline_capacity','build_primary_rows','build_secondary_rows','build_overflow_rows']
for key,r in lookup.items():
 w,k,level,f,arm,phase=key
 if k==1 and arm==1:
  ref=lookup[(w,k,level,f,0,phase)];assert all(r[x]==ref[x] for x in fields),(key,'map duplicate inventory differs')
 r['time_over_abseil_exact']=r['ns_per_operation']/lookup[(w,k,level,f,4,phase)]['ns_per_operation']
 r['logical_over_abseil_exact']=r['logical_bytes']/lookup[(w,k,level,f,4,phase)]['logical_bytes']
 r['unit_bytes']=192 if arm==2 else 128 if arm<3 else None
 r['value_count_after_build']=(6<<level)*(f if k==2 else 1)
 r['stays_inline_after_append']=k==2 and f<(r['inline_values'] if arm<3 else r['abseil_inline_capacity'])
 if phase=='update':
  initial=lookup[(w,k,level,f,arm,'hit')]
  r['net_inventory_unchanged']=all(r[x]==initial[x] for x in ['requested','owned_capacity','metadata'])
with (a.output/'comparison.csv').open('w') as f:
 writer=csv.DictWriter(f,fieldnames=list(dict.fromkeys(k for r in s['comparison'] for k in r)));writer.writeheader();writer.writerows(s['comparison'])
(a.output/'coverage.json').write_text(json.dumps(dict(rows=len(rows),processes=len(schedule),repetitions=a.repetitions,phases=9,cases=220,keys=[40],value_bytes=list(fans),kinds=['map','multimap'],fanouts=fans,source_elf_audits=audits,checks=['retained analysis report reproduction','exact seed912 serialized process schedule','all archived argv/returncodes/stderr and raw case chunks','audited ELF equals timed receipt for each width','duplicate map arm0/1 inventories and populations'],limitations=['Per-process repetitions are not independent machine trials.','Unchanged net inventory is not a direct transient-allocation counter.','Source-inline append branch and inventory together qualify selected allocation-free cases; no claim is made for arbitrary arrivals or other value widths.']),indent=2)+'\n')
print(f'Qualified {len(rows)} rows, {len(schedule)} processes, all three timed ELF identities and duplicate map controls')
