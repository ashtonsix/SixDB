#!/usr/bin/env python3
"""Validate and summarize bounded online-growth/publication comparisons."""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path

ARMS=('abseil','n8','n16','n16_direct')
COUNTS={
    'create_catalog':1, 'initial_build_publish':31,
    'list_point':31*64, 'list_cohort':31*64,
    'first_growth_publish':19, 'middle_point':50*64, 'middle_cohort':50*64,
    'second_growth_publish':47, 'final_point':97*64, 'final_cohort':97*64,
    'turnover_publish':97, 'aged_point':97*64, 'aged_cohort':97*64,
    'clear_release_publish':1, 'refill_publish':97, 'reclaim_history':1,
    'destroy_current':1,
}
BYTES=('live_payload_bytes','live_known_backing_bytes',
       'live_known_owner_metadata_bytes','shared_preparation_bytes')
MUTATIONS=tuple(p for p in COUNTS if not p.endswith(('_point','_cohort')))


def summarize(path):
    with path.open() as stream:
        raw=list(csv.DictReader(stream))
    groups={}
    for row in raw:
        arm=row['arm'];assert arm in ARMS,arm
        rep=int(row['repetition']);phase=row['phase']
        for name in ('trials','operations','checksum',*BYTES):
            row[name]=int(row[name]);assert row[name]>=0,(phase,name)
        row['ns_per_operation']=float(row['ns_per_operation'])
        assert math.isfinite(row['ns_per_operation']) and row['ns_per_operation']>=0
        groups.setdefault((arm,rep),[]).append(row)
    repetitions=sorted({rep for _,rep in groups})
    assert repetitions==list(range(len(repetitions))) and repetitions
    by_case={};diagnostics=[];cycle={a:[] for a in ARMS}
    for rep in repetitions:
        reference=None
        for arm in ARMS:
            rows=groups[arm,rep]
            timings=[r for r in rows if r['phase']!='migration_overlap']
            migrations=[r for r in rows if r['phase']=='migration_overlap']
            assert [r['phase'] for r in timings]==list(COUNTS),(arm,rep)
            contract=[(r['phase'],r['trials'],r['operations'],r['checksum']) for r in timings]
            if reference is None:reference=contract
            else:assert contract==reference,('operation/checksum mismatch',arm,rep)
            for r in timings:
                assert r['trials']>0 and r['operations']==COUNTS[r['phase']]*r['trials']
                by_case.setdefault((arm,r['phase']),[]).append(r)
            assert all(timings[-1][b]==0 for b in BYTES),('unreleased owner',arm,rep)
            cycle[arm].append(sum(r['ns_per_operation']*r['operations']/r['trials']
                                   for r in timings if r['phase'] in MUTATIONS))
            if arm=='abseil':assert not migrations
            else:assert [r['checksum'] for r in migrations]==[33,52,33,52]
            for i,r in enumerate(migrations):
                assert r['trials']==r['operations']==r['ns_per_operation']==0
                diagnostics.append({'arm':arm,'repetition':rep,'transition':i,
                                    'target_entries':r['checksum'],**{b:r[b] for b in BYTES}})
    summary=[]
    for (arm,phase),rows in by_case.items():
        values=[r['ns_per_operation'] for r in rows]
        baseline=statistics.median(r['ns_per_operation'] for r in by_case['abseil',phase])
        median=statistics.median(values)
        summary.append({'arm':arm,'phase':phase,'repetitions':len(values),
                        'median_ns_per_operation':median,'min':min(values),'max':max(values),
                        'abseil_over_candidate':baseline/median,
                        'sampled_accounting':{b:sorted({r[b] for r in rows}) for b in BYTES}})
    base=statistics.median(cycle['abseil'])
    cycles=[{'arm':arm,'median_ns_per_lifecycle':statistics.median(values),
             'min':min(values),'max':max(values),
             'abseil_over_candidate':base/statistics.median(values)} for arm,values in cycle.items()]
    return {'input':str(path),'timing_rows':sum(len(v) for v in by_case.values()),
            'phases':summary,'charged_mutation_publication_lifecycle':cycles,
            'sampled_migration_overlap':diagnostics}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs',nargs='+',help='target=operations.csv')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    results={}
    for item in args.inputs:
        target,path=item.split('=',1);assert target not in results
        results[target]=summarize(Path(path))
    document={
        'limits':[
            'Bounded K40/V64: lazy list then 64 and 128 primary slots; hot reads only.',
            'Timings aggregate different caller seeds; each accounting row samples only the first seed of that repetition.',
            'Mutation/publication lifecycle sum includes catalog creation, growth, turnover pairs, reset/refill, drain and destruction; excludes reads, verification, long-reader scope entry/exit and shared Pool setup/teardown.',
            'Turnover operations count erase/insert pairs; other mutation units are phase inputs, including any internal migration or head fixup.',
            'Payload/backing/known metadata/preparation are distinct quantities, not process RSS; Abseil backing is unknown.',
            'Shared_ptr control blocks and allocator rounding of owner metadata are not counted.',
            'Original reader and deferred drain retain intermediate versions; this is not minimum MVCC retention.',
            'Packed migration overlap has no Abseil rehash-peak counterpart; do not compare transient peaks.',
        ],
        'targets':results,
    }
    args.output.write_text(json.dumps(document,indent=2)+'\n')
    print(f'validated {sum(r["timing_rows"] for r in results.values())} timing rows across {len(results)} target(s)')
