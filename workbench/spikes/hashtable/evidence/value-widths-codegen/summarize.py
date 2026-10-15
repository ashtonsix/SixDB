#!/usr/bin/env python3
"""Qualify byte-width read consumers without assuming all outlining is harmless."""
import collections,csv,json,re,sys
from pathlib import Path
source,out=map(Path,sys.argv[1:3]);out.mkdir(parents=True,exist_ok=True)
a=json.loads(source.read_text());callees={r['address']:r for r in a['direct_callees']}
def address(c):
 m=re.match(r'(0x[0-9a-f]+)',c['target']);return m[1] if m else ''
def category(c):
 t=c['target']
 if 'ht::trial<' in t:return 'trial_closure'
 if any(s in t for s in ['__cxa_guard_acquire','__cxa_guard_release','__cxa_atexit','::formats()']):return 'cold_format_init'
 if any(s in t for s in ['__cxa_guard_abort','_Unwind','__clang_call_terminate','__cxa_throw','abort@','__assert_fail','ThrowStd','__throw_']):return 'failure_path'
 return 'other'
rows=[];failures=list(a['failures']);counts=collections.Counter()
for r in a['timed_roots']:
 entry={n:r[n] for n in ['K','value_bytes','kind','A','arm','phase','clock','instructions']}
 entry.update(phase_source_frames='|'.join(r['phase_source_frames']),wrapper_callsite=r.get('wrapper_callsite',''),end='|'.join(r['end']),calls='|'.join(c['target'] for c in r['calls']))
 issues=[]
 if r['unknown_branches']:issues.append(dict(reason='unresolved timed branch',branches=r['unknown_branches']))
 if any(c['indirect'] for c in r['calls']):issues.append(dict(reason='indirect timed call',calls=r['calls']))
 if r['phase'] in ['hit','miss','serial']:
  role='candidate' if r['arm']<3 else 'baseline'
  helpers=[c for c in r['calls'] if category(c)=='trial_closure']
  if helpers:
   if len(helpers)!=1 or len(r['calls'])!=1:issues.append(dict(reason='trial closure alongside other calls',calls=r['calls']))
   if any(c['inside_cfg_cycle'] for c in helpers):issues.append(dict(reason='trial closure repeated in timed CFG cycle',calls=helpers))
   for helper in helpers:
    if address(helper) not in callees:issues.append(dict(reason='missing closure body',call=helper));continue
    sub=callees[address(helper)];bad=[c for c in sub['calls'] if category(c) not in ['cold_format_init','failure_path'] or c['indirect']]
    if bad:issues.append(dict(reason='hot or indirect call within read closure',calls=bad))
    branches=[c for c in sub['external_branches'] if category(c)!='failure_path' or c['indirect']]
    if branches:issues.append(dict(reason='unresolved external branch within closure',branches=branches))
    entry.update(helper_address=sub['address'],helper_normalized_sha256=sub['normalized_sha256'],helper_instructions=sub['instructions'],helper_calls='|'.join(c['target'] for c in sub['calls']))
   location='whole_loop'
  else:
   bad=[c for c in r['calls'] if category(c) not in ['cold_format_init','failure_path']]
   if bad:issues.append(dict(reason='hot call within timed read',calls=bad))
   location='inline'
  counts[role+('_unqualified' if issues else '_'+location)]+=1
 entry['qualified']=not issues;rows.append(entry)
 if issues:failures.append(dict(K=r['K'],value_bytes=r['value_bytes'],kind=r['kind'],A=r['A'],arm=r['arm'],phase=r['phase'],issues=issues))
with (out/'codegen.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(dict.fromkeys(k for r in rows for k in r)));w.writeheader();w.writerows(rows)
summary={n:a[n] for n in ['value_bytes','key_bits','assembly_sha256','binary_sha256','source_sha256','compile_command','compile_commands_sha256']}
summary.update(qualified=not failures,phase_assignment='DWARF caller assignments in supplied frozen source; multiple reachable ending clocks must share phase; no clock ordinal or lambda number',timed_regions=len(rows),read_regions=sum(r['phase'] in ['hit','miss','serial'] for r in rows),outlined_timing_wrappers=sum('wrapper_callsite' in r for r in a['timed_roots']),regions_with_cloned_ending_clock=sum(len(r['end'])>1 for r in a['timed_roots']),read_locations=dict(counts),failures=failures,limitations=['Compile receipt binds W/K macro selection; supplied source and actual ELF are hashed. The receipt is not a reproducible-build proof.','A trial closure is called a whole-loop helper only outside a caller CFG cycle and with no hot calls or unresolved external branches inside.','Cold format and failure calls are reported separately. This audit does not assign runtime branch frequencies or costs.','Full-byte-load/dataflow inspection is a separate selected-witness check; fused calls alone do not prove the digest consumes every byte.'])
(out/'codegen-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ['compile_command','failures','limitations']}))
if failures:
 print('Unqualified regions / phase failures:',len(failures));print(json.dumps(failures[:3],indent=2));sys.exit(1)
