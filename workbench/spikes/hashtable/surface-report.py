#!/usr/bin/env python3
"""Validate native surface receipts; retain individual cases and expose losses."""
import argparse
import collections
import csv
import gzip
import json
import math
import re
import statistics
from pathlib import Path

WIDTHS=set(range(8,65,8))
KINDS={0,1,2}
ARMS={'compact_list','abseil','abseil_exact'}


def read_csv(path):
    rows=list(csv.DictReader(path.open()))
    assert rows, path
    for r in rows:
        for k,v in list(r.items()):
            if k in {'arm','phase','operation'}:continue
            r[k]=float(v) if k in {'ns','lifecycle_ns','ns_per_op'} else int(v)
            assert math.isfinite(r[k]) and r[k]>=0,(path,k,v)
    return rows


def group_rows(rows,fields):
    groups=collections.defaultdict(list)
    for r in rows:groups[tuple(r[k] for k in fields)].append(r)
    repeats=sorted({r['rep'] for r in rows})
    assert repeats==list(range(len(repeats)))
    for key,samples in groups.items():
        assert sorted(x['rep'] for x in samples)==repeats,(key,'duplicate/missing repetitions')
    return groups,repeats


def numeric_summary(samples,fields):
    return {k:statistics.median(x[k] for x in samples) for k in fields}


def with_ratios(items,dimensions,metric,baselines):
    lookup={tuple(x[k] for k in dimensions)+(x['arm'],):x for x in items}
    for x in items:
        for baseline in baselines:
            other=lookup.get(tuple(x[k] for k in dimensions)+(baseline,))
            if other and other[metric]>0:x['ratio_to_'+baseline]=x[metric]/other[metric]
    return items


def small(path):
    rows=read_csv(path);fields=('arm','K','kind','n','phase')
    groups,reps=group_rows(rows,fields)
    expected=set()
    for arm in ARMS:
        for k in WIDTHS:
            for kind in KINDS:
                for n in (1,4,16,64,256):
                    phases={'construct','build','hit','get4','get8','aged_hit','erase','release',
                            'serial_caller_graph' if kind==0 else 'serial_value_graph',
                            'promote4' if kind==2 else 'set4','append8' if kind==2 else 'set8'}
                    if k!=8 or n!=256:phases.add('miss')
                    expected|={(arm,k,kind,n,p) for p in phases}
    assert set(groups)==expected,('small coverage',len(groups),len(expected))
    out=[]
    for key,samples in sorted(groups.items()):
        assert len({x['operations'] for x in samples})==1
        times=[x['ns']/x['operations'] for x in samples]
        out.append(dict(zip(fields,key))|dict(repetitions=len(reps),ns_per_operation=statistics.median(times),
            min_ns_per_operation=min(times),max_ns_per_operation=max(times),
            **numeric_summary(samples,['operations','requested','owned_capacity','metadata'])))
    return with_ratios(out,('K','kind','n','phase'),'ns_per_operation',('abseil','abseil_exact'))


def batch(path):
    rows=read_csv(path);fields=('arm','K','kind','initial_keys','extended_history','N')
    groups,reps=group_rows(rows,fields)
    expected={(a,k,t,n,h,N) for a in ARMS for k in WIDTHS for t in KINDS
              for n in (1,16,128,240) for h in range(2 if t==2 else 1) for N in (1,4,8)}
    assert set(groups)==expected,('batch coverage',len(groups),len(expected))
    outputs=collections.defaultdict(set);out=[]
    metrics=['build_operations','write_operations','queries','hits','values','digest']
    for key,samples in sorted(groups.items()):
        for r in samples:outputs[key[1:5]].add(tuple(r[k] for k in metrics))
        times=[r['lifecycle_ns']/(r['build_operations']+r['write_operations']+r['queries']) for r in samples]
        out.append(dict(zip(fields,key))|dict(repetitions=len(reps),ns_per_operation=statistics.median(times),
            min_ns_per_operation=min(times),max_ns_per_operation=max(times),
            **numeric_summary(samples,['lifecycle_ns',*metrics,'initial_requested','initial_owned_capacity',
                'initial_metadata','final_requested','final_owned_capacity','final_metadata'])))
    assert all(len(x)==1 for x in outputs.values()),'batch arms/N did not consume identical logical histories'
    return with_ratios(out,('K','kind','initial_keys','extended_history','N'),'ns_per_operation',('abseil','abseil_exact'))


def publication(path):
    rows=read_csv(path);fields=('arm','K','kind','phase');groups,reps=group_rows(rows,fields)
    phases={'initial_build_publish','promotion_clone_publish','grow_and_bags_publish',
            'erase_reuse_publish','clear_refill_publish','reclaim_history'}
    assert set(groups)=={(a,k,t,p) for a in ('compact_list','abseil') for k in WIDTHS for t in KINDS for p in phases}
    out=[]
    for key,samples in sorted(groups.items()):
        for r in samples:
            # retained includes current; adding current again would double count.
            r['charged_requested_and_metadata']=r['retained_requested']+r['retained_external_metadata']+r['catalog_live']
        out.append(dict(zip(fields,key))|dict(repetitions=len(reps),**numeric_summary(samples,
            ['ns','current_requested','current_capacity','current_metadata','retained_requested','retained_capacity',
             'retained_external_metadata','catalog_live','charged_requested_and_metadata'])))
    return with_ratios(out,('K','kind','phase'),'ns',('abseil',))


def transition(path):
    rows=read_csv(path);fields=('arm','K','kind','n','phase');groups,reps=group_rows(rows,fields)
    expected=set()
    for a in ('list_hierarchy','abseil'):
        for k in WIDTHS:
            for t in KINDS:
                for n in (16,64,128,256):
                    phases={'construct','build','promote','read_after','release','lifetime_excluding_reads',
                            'serial_caller_cycle_after' if t==0 else 'serial_value_cycle_after'}
                    if k!=8 or n!=256:phases.add('insert_after')
                    expected|={(a,k,t,n,p) for p in phases}
    assert set(groups)==expected,('transition coverage',len(groups),len(expected))
    out=[]
    for key,samples in sorted(groups.items()):
        out.append(dict(zip(fields,key))|dict(repetitions=len(reps),
            ns_per_operation=statistics.median(x['ns']/x['operations'] for x in samples),
            **numeric_summary(samples,['ns','operations','requested','owned_capacity','metadata'])))
    # Promote is an Abseil no-op; no ratio against its timer overhead is meaningful.
    comparable=[x for x in out if x['phase']!='promote']
    with_ratios(comparable,('K','kind','n','phase'),'ns_per_operation',('abseil',))
    return out


def rich(path):
    rows=read_csv(path);fields=('arm','row_bytes','active','buckets','operation')
    groups,reps=group_rows(rows,fields);out=[]
    buckets={r['buckets'] for r in rows};assert len(buckets)==1
    assert len({r.get('fingerprint_bits',7) for r in rows})==1,'mixed fingerprint widths in one rich process'
    expected={(a,w,n,b,p) for a in ('sp_tp_prepared','sp_tp_admitted','sp_tp_split','raw_fused')
              for w in (6,10) for n in (0,1,7,8,15,16) for b in buckets
              for p in ('independent_hit','fingerprint_miss','suffix_miss','serial_hit','changed_update')}
    assert set(groups)==expected,'rich coverage'
    checks=collections.defaultdict(set)
    for key,samples in sorted(groups.items()):
        for r in samples:checks[key[1:]].add(r['checksum'])
        out.append(dict(zip(fields,key))|dict(repetitions=len(reps),fingerprint_bits=samples[0].get('fingerprint_bits',7),
            **numeric_summary(samples,['ns_per_op','payload_bytes','shared_prepared_bytes','queries','rounds'])))
    assert all(len(x)==1 for x in checks.values()),'rich full-value checksum mismatch'
    return with_ratios(out,('row_bytes','active','buckets','operation'),'ns_per_op',('raw_fused',))


def outer_callee(instruction):
    """Ignore callback template arguments when naming a direct C++ callee."""
    match=re.search(r'<(.*)>',instruction)
    if not match:return None
    depth=0;parameters=0;name=[]
    for char in match[1]:
        if char=='<':depth+=1
        elif char=='>':depth-=1
        elif depth==0:
            if char=='(':parameters+=1
            elif char==')':parameters-=1
            elif parameters==0:name.append(char)
    return ''.join(name)


def codegen(root):
    out={}
    for path in sorted((root/'codegen').glob('*.s.gz')):
        bodies={};current=None
        with gzip.open(path,'rt') as stream:
            for line in stream:
                m=re.match(r'^[0-9a-f]+ <(.+)>:$',line.strip())
                if m:current=m[1];bodies[current]=[]
                elif current is not None:bodies[current].append(line.strip())
        name=path.name.removesuffix('.s.gz')
        selected={k:v for k,v in bodies.items() if k.startswith(('rich_','ht_hash_packet')) or
                  re.search(r'\bht::batch_receipt::(?:ingest|consume|lifecycle)<',k)}
        witnesses={}
        for symbol,body in selected.items():
            instructions=[s for s in body if re.match(r'^[0-9a-f]+:',s)]
            calls=[s for s in instructions if re.search(r'\s(?:bl|blr|callq?)\s',s)]
            stack=[s for s in instructions if re.search(r'\bsp\b|%rsp|%esp|\bpush[qwl]?\b|\bpop[qwl]?\b',s)]
            callees=[outer_callee(s) for s in calls]
            named_searches=[s for s,c in zip(calls,callees) if c and
                            c.rsplit('::',1)[-1].split()[0] in {'getN','setN','set_value','get','find'}]
            is_batch_root=symbol.startswith(('unsigned int ht::batch_receipt::ingest<',
                'ht::batch_receipt::answer ht::batch_receipt::consume<','double ht::batch_receipt::lifecycle<'))
            witnesses[symbol]=dict(instructions_including_alignment=len(instructions),calls=calls,
                direct_outer_callees=callees,is_batch_root=is_batch_root,
                simd_unsigned_widening_multiplies=sum(bool(re.search(r'\b(?:umull2?|vpmuludq)\b',s)) for s in instructions),
                named_search_or_batch_calls=named_searches,stack_instructions=stack)
        if name=='hash_packet_check':
            required=['ht_hash_packet4_64a20','ht_hash_packet8_64a20','ht_hash_packet8_64a0','ht_hash_packet8_16a9']
            assert all(x in bodies for x in required),'missing hash SIMD witnesses'
            for x in required:
                assert any(re.search(r'\b(?:umull2?|vpmuludq)\b',s) for s in bodies[x]),('missing explicit SIMD multiply',x)
        if name=='rich_composition':
            required=[f'rich_{arm}{width}_{op}' for arm in ('composed','admitted','split','fused') for width in (6,10) for op in ('get','update')]
            assert all(x in bodies for x in required),'missing rich witnesses'
        out[name]=dict(symbols=len(bodies),witnesses=witnesses)
    (root/'codegen.json').write_text(json.dumps(out,indent=2)+'\n')
    return out


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('results',type=Path)
    parser.add_argument('--codegen-only',action='store_true');args=parser.parse_args();root=args.results
    facts=codegen(root)
    if args.codegen_only:
        print('Codegen witnesses retained for '+', '.join(facts));return
    sections={'small':small(root/'small.csv'),'small_scan64':small(root/'small_scan64.csv'),'batch':batch(root/'small_batch.csv'),
              'publication':publication(root/'small_publication.csv'),'transition':transition(root/'list_transition.csv')}
    ordinary={(r['K'],r['kind'],r['n'],r['phase']):r for r in sections['small'] if r['arm']=='compact_list'}
    for r in sections['small_scan64']:
        if r['arm']=='compact_list':
            before=ordinary[r['K'],r['kind'],r['n'],r['phase']]
            r['ratio_to_scan16_control']=r['ns_per_operation']/before['ns_per_operation']
    for path in sorted(root.glob('rich-*.csv')):sections[path.stem]=rich(path)
    scope=[
        'Native single-worker measurements; no cache-level or DRAM-residence claim follows from allocation size.',
        'Requested bytes, allocator-owned capacity and metadata are separate. Abseil capacity=0 means unreported, not zero allocation.',
        'Baseline nested bag requested capacity is inventoried outside timing; allocator bookkeeping/slack and exact nested peaks are not measured.',
        'Process maximum RSS is in each resources file and includes driver/oracle/runtime state; it is not per-container RSS.',
        'Batch lifecycle includes fresh construction, build, matched writes, consuming every returned value and destruction; pool setup/drain excluded.',
        'Batch scalar/4/8 use identical histories. small.csv MM promote4/append8 are different lifecycle stages, not an N comparison.',
        'Publication uses complete material cloning/capture and retained readers; it does not establish a fine-grained MVCC damage consumer.',
        'Both captured publication clone paths reconstruct by iteration/insertion; candidate pre-reserves count, while Abseil does not reserve or use its native copy constructor. These timings do not establish strongest native-copy/COW parity.',
        'Publication retained inventory includes current ownership; charged total adds retained payload, external metadata and live catalog once.',
        'Transition promote has no Abseil analogue; no ratio against its timer-only baseline. Full-K8 zero-insertion rows are omitted.',
        'Transition lifetime_excluding_reads sums the timed construction/build/promotion/insertion/destruction phases; intervening checks and reads are excluded.',
        'small_scan64 is an alternative explicit wider fingerprint scan, not adopted. Its ratio to scan16 compares medians from separate complete processes.',
        'Rich SP/TP split is the stronger existing-primitive projection; original prepared/admitted projections are diagnostics.',
        'Rich serial timings include the identical full-value consumer and answer-dependent trace lookup; empty-active rows are controls.',
        'This is the bounded compact-list/publication/transition surface, not large-table Loom throughput, multi-writer scaling or arbitrary key distributions.'
    ]
    (root/'summary.json').write_text(json.dumps(dict(scope=scope,cases=sections),indent=2)+'\n')
    for section,cases in sections.items():
        baseline='raw_fused' if section.startswith('rich-') else 'abseil'
        candidate='sp_tp_split' if section.startswith('rich-') else 'list_hierarchy' if section=='transition' else 'compact_list'
        selected=[x for x in cases if x['arm']==candidate and 'ratio_to_'+baseline in x]
        losses=sorted(selected,key=lambda x:x['ratio_to_'+baseline],reverse=True)
        print(f'{section}: {len(cases)} validated cases; {sum(x["ratio_to_"+baseline]>1.05 for x in selected)}/{len(selected)} candidate cells >5% slower than {baseline}')
        for x in losses[:8]:
            label={k:x[k] for k in ('K','kind','n','initial_keys','extended_history','N','phase','row_bytes','active','buckets','operation') if k in x}
            print(' ',json.dumps(label,sort_keys=True),f'{x["ratio_to_"+baseline]:.3f}x')
    print('Ratios are candidate time / baseline time; >1 is slower. Cell counts are unweighted diagnostics, not an aggregate score.')
    for line in scope:print(line)


if __name__=='__main__':main()
