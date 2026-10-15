#!/usr/bin/env python3
"""Summarize verified complete timed CFGs and actual whole-loop helper calls."""
import csv,json,re,sys,collections
from pathlib import Path
source,out=map(Path,sys.argv[1:3]);expected=int(sys.argv[3]) if len(sys.argv)>3 else 1800;baseline=int(sys.argv[4]) if len(sys.argv)>4 else 6;out.mkdir(parents=True,exist_ok=True)
a=json.loads(source.read_text());assert not a['failures'];assert len(a['timed_roots'])==expected
callees={r['address']:r for r in a['direct_callees']}
def address(c):return re.match(r'(0x[0-9a-f]+)',c['target'])[1]
def kind(c):
 t=c['target']
 if 'format_trial<' in t:return 'whole_read_loop'
 if any(s in t for s in ['__cxa_guard_acquire','__cxa_guard_release','__cxa_atexit','::formats()']):return 'cold_format_init'
 if any(s in t for s in ['__cxa_guard_abort','_Unwind','__clang_call_terminate','__cxa_throw']):return 'failure_path'
 if 'record_table<' in t and '::erase<' in t:return 'record_erase'
 if 'EraseMetaOnly' in t:return 'baseline_erase_metadata'
 if 'operator delete' in t:return 'deallocation'
 return t.split(' <',1)[-1].removesuffix('>')
rows=[];counts=collections.Counter()
for r in a['timed_roots']:
 assert not r['unknown_branches'] and not any(c['indirect'] for c in r['calls'])
 entry={n:r[n] for n in ['K','kind','A','arm','phase','clock','instructions']};entry.update(phase_source_frames='|'.join(r['phase_source_frames']),wrapper_callsite=r.get('wrapper_callsite',''),end='|'.join(r['end']),calls='|'.join(kind(c)+'@'+address(c) for c in r['calls']))
 if r['phase'] in ('hit','miss','serial'):
  helpers=[c for c in r['calls'] if kind(c)=='whole_read_loop'];role='candidate' if r['arm']<baseline else 'baseline'
  if helpers:
   assert len(helpers)==len(r['calls'])==1,(r['K'],r['arm'],r['phase'])
   assert not helpers[0]['inside_cfg_cycle'],(r['K'],r['arm'],r['phase'],'per-key closure is not a whole-loop helper')
   sub=callees[address(helpers[0])]
   assert all(kind(c) in ['cold_format_init','failure_path'] for c in sub['calls']),(r['K'],r['arm'],sub['calls'])
   entry.update(helper_address=sub['address'],helper_normalized_sha256=sub['normalized_sha256'],helper_instructions=sub['instructions'],helper_call_kinds='|'.join(sorted({kind(c) for c in sub['calls']})))
   counts[role+'_whole_loop']+=1
  else:
   assert all(kind(c) in ['cold_format_init','failure_path'] for c in r['calls']),(r['K'],r['arm'],r['phase'],r['calls'])
   counts[role+'_inline']+=1
 rows.append(entry)
with (out/'codegen.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(dict.fromkeys(k for r in rows for k in r)));w.writeheader();w.writerows(rows)
summary=dict(assembly_sha256=a['assembly_sha256'],binary_sha256=a['binary_sha256'],source_sha256=a['source_sha256'],outlined_timing_wrappers=sum('wrapper_callsite' in r for r in a['timed_roots']),phase_assignment='DWARF inline caller locations matched to frozen source assignments; no clock ordinal or lambda number',timed_regions=expected,read_regions=sum(r['phase'] in ('hit','miss','serial') for r in rows),unknown_branches=0,indirect_calls=0,per_key_or_value_calls_in_read_loops=0,read_locations=dict(counts))
(out/'codegen-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
