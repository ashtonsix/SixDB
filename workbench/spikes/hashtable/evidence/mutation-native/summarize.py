#!/usr/bin/env python3
"""Qualify copied-variant read consumers and retain update call boundaries without frequency claims."""
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
summary={n:a[n] for n in ['variant','value_bytes','key_bits','assembly_sha256','binary_sha256','source_sha256','compile_command','probe_receipt_sha256','generated_header_sha256']}
summary.update(qualified=not failures,update_callgraph_truncated=a['update_callgraph_truncated'],phase_assignment='DWARF caller assignments in supplied frozen source; multiple reachable ending clocks must share phase; no clock ordinal or lambda number',timed_regions=len(rows),read_regions=sum(r['phase'] in ['hit','miss','serial'] for r in rows),outlined_timing_wrappers=sum('wrapper_callsite' in r for r in a['timed_roots']),regions_with_cloned_ending_clock=sum(len(r['end'])>1 for r in a['timed_roots']),read_locations=dict(counts),failures=failures,limitations=['Probe receipt binds variant/W/K selection and verifies copied TU, generated headers and actual ELF. This is not a reproducible-build proof.','A trial closure is called a whole-loop helper only outside a caller CFG cycle and with no hot calls or unresolved external branches inside.','Cold format and failure calls are reported separately. This audit does not assign runtime branch frequencies or costs.','Full-byte-load/dataflow inspection is a separate selected-witness check; fused calls alone do not prove the digest consumes every byte.'])
# Mutation calls are evidence, not a failure just because the control outlines
# an owner operation. A call in a query CFG cycle can lie behind a branch that
# only executes for spill/refusal; static recurrence does not establish heat.
def mutation_category(call):
 t=call['target'];base=category(call)
 if base in ['cold_format_init','failure_path']:return base
 if base=='trial_closure':return 'trial_closure'
 if 'ht::promoted_table<' in t:return 'promoted_owner'
 if 'ht::record_table<' in t:return 'index_owner'
 link=re.search(r'ht::links<(\d+)u,',t)
 if link:return 'linked_values' if int(link[1])==0 else 'key_overflow'
 if 'ht::linked_bags<' in t:return 'linked_values'
 if 'ikea::' in t:return 'packing_primitive'
 if 'orbital::' in t:return 'orbital_owner_runtime'
 if any(x in t for x in ['operator new','operator delete','malloc@','calloc@','realloc@','free@','<_ZdlPv','<_ZdaPv','<_Znwm','<_Znam']):return 'allocation_runtime'
 if any(x in t for x in ['__cxa_begin_catch','__cxa_end_catch']):return 'exception_handling'
 if any(x in t for x in ['memcpy@','memmove@','memset@']):return 'byte_transfer'
 if 'absl::' in t:return 'abseil_boundary'
 if 'ht::' in t:return 'other_spike_boundary'
 return 'other'
updates=[];update_issues=[]
for r in a['timed_roots']:
 if r['phase']!='update':continue
 helpers=[c for c in r['calls'] if category(c)=='trial_closure' and not c['inside_cfg_cycle']]
 complete_helper=len(r['calls'])==1 and len(helpers)==1 and address(helpers[0]) in callees
 if complete_helper:
  body=callees[address(helpers[0])];effective=body['calls'];location='whole_loop_helper';body_address=body['address']
  unresolved=[c for c in body['external_branches'] if category(c)!='failure_path' or c['indirect']]
 else:effective=r['calls'];location='timed_cfg';body_address=None;unresolved=r['unknown_branches']
 if unresolved or any(c['indirect'] for c in effective):
  update_issues.append(dict(K=r['K'],value_bytes=r['value_bytes'],kind=r['kind'],A=r['A'],arm=r['arm'],unresolved_branches=unresolved,indirect_calls=[c for c in effective if c['indirect']]))
 direct=[{**c,'category':mutation_category(c)} for c in effective]
 paths=[];pending=[(c,[]) for c in effective];seen=set()
 while pending:
  call,path=pending.pop();dest=address(call);edge={**call,'category':mutation_category(call)};chain=path+[edge]
  paths.append(chain)
  if dest in seen or dest not in callees or len(chain)>=8:continue
  seen.add(dest);sub=callees[dest]
  if 'ht::' not in sub['symbol']:continue
  pending.extend((c,chain) for c in sub['calls'])
 updates.append({k:r[k] for k in ['K','value_bytes','kind','A','arm','clock']}|dict(location=location,helper_address=body_address,effective_calls=direct,reachable_call_paths=paths,unresolved_branches=unresolved))
mutation=dict(variant=a['variant'],value_bytes=a['value_bytes'],binary_sha256=a['binary_sha256'],updates=updates,callgraph_truncated=a['update_callgraph_truncated'],limitations=['inside_cfg_cycle is static reachability, not measured execution count or probability.','An allocation/runtime call inside a query loop may be conditional on spill; no dynamic-hot label is inferred.','Owner/preparation boundaries include full demangled nested-lambda names; their outer class prefix is not the actual callee identity.','Read fusion and update boundary structure are separate; the control may intentionally retain repeated owner calls.'])
(out/'mutation-paths.json').write_text(json.dumps(mutation,indent=2)+'\n')
summary['read_and_root_qualified']=not failures
summary['update_effective_cfg_qualified']=not update_issues
failures.extend(update_issues);summary['qualified']=not failures
summary['update_regions']=len(updates)
summary['update_locations']=dict(collections.Counter(x['location'] for x in updates))
summary['repeated_update_boundary_categories']=dict(collections.Counter(c['category'] for x in updates for c in x['effective_calls'] if c['inside_cfg_cycle']))
(out/'codegen-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ['compile_command','failures','limitations']}))
if failures:
 print('Unqualified regions / phase failures:',len(failures));print(json.dumps(failures[:3],indent=2));sys.exit(1)
