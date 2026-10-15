#!/usr/bin/env python3
"""Rebuild the concise scorecard from retained, already-qualified comparisons."""
import argparse,csv,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parent
lines=['# Current head-to-head evidence\n',
'**Keep the hierarchy; do not claim broad superiority or a universal selected preset.** Cold Loom throughput and narrow logical memory are real strengths. Compact lookup, common warm reads, wide multimaps and some growth/merge histories remain losses. These are different measured configurations, not the combined performance of one final owner.\n',
'Every pair below is **ours / Abseil**, in absolute units. Serial latency means an answer-dependent chain with one request in flight. Reciprocal throughput is elapsed time per completed operation with independent work or the stated Ring concurrency. Memory is logical requested storage plus reported owner metadata; it is not per-container RSS, allocation backing or retained peak memory.\n',
'Native platforms are Zen5 AVX2, V2 NEON and Granite AVX-512. SVE2/V3 are not qualified by these results. Read all three objectives before interpreting a win.\n',
'## Warm single-writer comparison\n',
'Corrected 6,144-key singleton histories, medium 14-slot family, doubling/90%. These are warmed workloads, not separately established L1/L2 tiers. Independent reads measure reciprocal throughput; dependent reads measure serial latency. Full build/release and exact-width baseline rows are in [benchmark validation](BENCHMARK-VALIDATION.md#native-corrected-comparison). Earlier allocating-validator lifetime advantages are superseded.\n',
'| Host / case | Dependent hit ns | Independent hit ns | Logical B |',
'| --- | ---: | ---: | ---: |']
warm={}
for line in (root/'BENCHMARK-VALIDATION.md').read_text().splitlines():
 if line.startswith('| Zen5 |') or line.startswith('| V2 |'):
  cells=[c.strip() for c in line.strip('|').split('|')];warm[tuple(cells[:3])]=cells
for host in ['Zen5','V2']:
 for case in ['K40 set','K40 W2 map','K40 W2 multimap','K64 W64 map','K64 W64 multimap']:
  rows=[warm[(host,case,arm)] for arm in ['hierarchy','Abseil']]
  lines.append('| '+host+' / '+case+' | '+' | '.join(' / '.join(r[i] for r in rows) for i in [6,5,7])+' |')
lines+=['\nThe narrow logical-memory advantage also survives the exact-key comparison: K40 set 38,398 B versus 49,186/49,178 B (Zen/V2), K40/W2 map 55,788 B versus 65,568/65,560 B. The exact-key competitor can be faster or slower than ordinary Abseil depending on the operation; see the complete paired rows.\n',
'## Loom: serial latency and reciprocal throughput\n',
'1,572,864-key all-hit cohorts, medium family. Serial is the direct C1 path; throughput is C32 Ring with matched caller-input hints. Wide maps use a located-value stage; other rows use whole-bucket hints. Each Abseil throughput cell is the faster direct/prefetch median **in that same process**. No baseline or memory is borrowed from a different policy. These cohorts do not all exceed each host’s LLC.\n',
'| Host / case | Serial C1 ns | Ring C32 ns/query | Logical B |',
'| --- | ---: | ---: | ---: |']
read={}
for host,title in [('zen5','Zen5'),('neon','V2'),('granite','Granite')]:
 path=root/f'evidence/read-windows-{host}'/('panel/summary.csv' if host=='granite' else 'summary.csv')
 rows=list(csv.DictReader(path.open()));read[host]=rows
 for k,kind,w,label,policy in [('40','0','0','K40 set','0'),('40','1','2','K40/W2 map','0'),('64','1','64','K64/W64 map','2'),('40','2','2','K40/W2 multimap','0'),('64','2','64','K64/W64 multimap','0')]:
  rs=[r for r in rows if (r['K'],r['kind'],r['W'],r['keys'],r['capacity_family'],r['read_windows'],r['mixed'])==(k,kind,w,'1572864','14',policy,'0')]
  def one(owner,method,c):
   found=[r for r in rs if (r['owner'],r['method'],r['concurrency'])==(owner,method,c)];assert len(found)==1;return found[0]
  serial=[one(o,'direct','1') for o in ['generic','abseil']]
  throughput=[one('generic','ring_hints','32'),min([one('abseil',m,'32') for m in ['direct','prefetch']],key=lambda r:float(r['ns_per_request']))]
  lines.append('| '+title+' / '+label+' | '+' / '.join(f"{float(r['ns_per_request']):.2f}" for r in serial)+' | '+' / '.join(f"{float(r['ns_per_request']):.2f}" for r in throughput)+' | '+' / '.join(f"{int(r['logical_bytes']):,}" for r in serial)+' |')
lines+=['\nThe additional **Granite above-LLC K64/W64 map** has 12,582,912 keys, with a 1,040,187,392 B primary plus 94,371,840 B secondary allocation; primary alone exceeds the 480 MiB LLC. Its matched located-value process gives:\n',
'| Queries | Serial C1 ns | Ring C32 ns/query | Logical B |',
'| --- | ---: | ---: | ---: |',
'| Hits | 365.92 / 238.43 | 40.53 / 49.41 | 1,137,952,410 / 1,224,736,376 |',
'| Mixed | 326.10 / 278.25 | 44.53 / 51.35 | 1,137,952,410 / 1,224,736,376 |',
'\nThese are a throughput/memory trade-off with a substantial serial loss. [Read-window evidence](READ-WINDOWS.md) includes all controls, raw data, physical receipts and the Granite timeout selection: only complete matched groups are retained. No per-request PMU evidence proves every access is served by DRAM.\n',
'## Actual Worker updates\n',
'Two overlapping owners, 1,024 keys, 128 updates/key. Cost includes intake and coordinated growth, in **reciprocal ns/update**, not serial latency. Narrow/set/MM rows use the incumbent geometry; wide maps use the 14-slot control. Each baseline is the matched process. Logical bytes cover both active owners, with runtime/queue/inactive state and Pool/backing reported separately in the [full format comparison](worker-epochs.md#solver-sized-formats-in-the-complete-worker-caller).\n',
'| Host / case | Update ns | Active logical B |',
'| --- | ---: | ---: |']
cases=[('40','0','0','K40 set'),('40','1','2','K40/W2 map'),('64','1','64','K64/W64 map'),('40','2','2','K40/W2 multimap'),('64','2','64','K64/W64 multimap')]
for host,title in [('zen5','Zen5'),('neon','V2'),('granite','Granite')]:
 rows=list(csv.DictReader((root/f'evidence/worker-format-{host}/qualified/summary.csv').open()))
 for k,kind,w,label in cases:
  family='14' if (kind,w)==('1','64') else '0'
  rs={r['arm']:r for r in rows if (r['K'],r['kind'],r['W'],r['input_keys'],r['passes'],r['partitioned'],r['skew'],r['slot_family'])==(k,kind,w,'1024','128','0','0',family)};assert len(rs)==2
  lines.append('| '+title+' / '+label+' | '+' / '.join(f"{float(rs[x]['ns_per_update']):.2f}" for x in ['hierarchy','abseil'])+' | '+' / '.join(f"{int(float(rs[x]['logical'])):,}" for x in ['hierarchy','abseil'])+' |')
lines+=['\n## Actual Worker folding\n',
'768 keys/source, 32 updates/key, **disjoint sources** folded into the existing destination. Sets/maps transfer one reduced record/key; multimaps transfer all 32 values/key. Cost is ns/transferred item, including serialized bounded handoff and destination growth. Memory describes the final destination. Full overlap, partial overlap, tiny tables, build/retirement and coexistence are retained in [Worker folding](worker-fold.md#completed-evidence). This is reciprocal transfer cost, not lookup latency.\n',
'| Host / case | Fold ns/item | Destination logical B |',
'| --- | ---: | ---: |']
for host,title in [('zen5','Zen5'),('neon','V2'),('granite','Granite')]:
 rows=[]
 for path in sorted((root/f'evidence/worker-fold-{host}').glob('*-order*.csv')):
  with path.open() as f:
   reader=csv.reader(f);header=next(reader);data=list(reader)
  if header[-1]=='slot_family' and all(len(r)==len(header)+3 for r in data):header+=['inactive_bytes','context_bytes','inspection_bytes']
  assert all(len(r)==len(header) for r in data);rows += [dict(zip(header,r)) for r in data]
 for k,kind,w,label in cases:
  rs={a:[r for r in rows if (r['K'],r['kind'],r['W'],r['keys'],r['passes'],r['overlap'],r['arm'])==(k,kind,w,'768','32','0',a)] for a in ['hierarchy','abseil']}
  assert all(len(v)==10 for v in rs.values())
  lines.append('| '+title+' / '+label+' | '+' / '.join(f"{statistics.median(float(r['fold_ns_per_input']) for r in rs[x]):.2f}" for x in ['hierarchy','abseil'])+' | '+' / '.join(f"{statistics.median(int(r['logical']) for r in rs[x]):,.0f}" for x in ['hierarchy','abseil'])+' |')
lines+=['\nAll three native fold jobs completed their 102 timed processes and 34 checks, then failed final reporting because three CSV labels were omitted. The strict recovery report identifies that exact schema and reproduces summaries from unchanged retained measurements. Source/output provenance and the original failures remain preserved.\n',
'## How to use this evidence\n',
'The architecture is promising, but the multimap and lifetime gaps are not minor hot-latency costs. [Graduation assessment](GRADUATION.md) distinguishes resolved choices, implemented boundaries and remaining work. [Historical results](RESULTS-HISTORICAL.md) preserve attribution without serving as the current scorecard. Rebuild this page with `python3 workbench/spikes/hashtable/current-scorecard.py --output workbench/spikes/hashtable/RESULTS.md`; native fold reports have their own strict qualification/recovery recipes.\n']
out='\n'.join(lines)
if a.output:a.output.write_text(out)
else:print(out,end='')
