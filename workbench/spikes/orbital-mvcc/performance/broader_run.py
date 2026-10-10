#!/usr/bin/env python3
"""Bounded selected-composition growth and staged-request discovery panel."""
import argparse,csv,hashlib,json,os,statistics,sys
from pathlib import Path
STUDY=Path(__file__).resolve().parent
ROOT=STUDY.parents[3]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from experiment import Run

def panel(pilot=False):
    if pilot:
        for rows,objects in [(2097152,64),(8388608,256)]:
            for execution,width,hints in [('fused',1,0),('direct',1,0),('manual',8,0),('ring',8,0),('ring',32,1)]:
                yield dict(panel='ring',rows=rows,objects=objects,ops=524291,execution=execution,width=width,hints=hints,mode='throughput',repetition=0)
        return
    sizes=[(32768,1),(2097152,64),(8388608,256)]
    for rows,objects in sizes:
        arms=[('fused',1,0),('direct',1,0),('serial',1,0),('manual',8,0),('ring',8,0),('ring',32,1)]
        if objects==256:
            arms=[('fused',1,0),('direct',1,0),('serial',1,0),('serial',1,1)]
            arms += [(execution,width,hints) for execution in ['manual','ring'] for width in [1,8,32] for hints in [0,1]]
        for repetition in range(2):
            ordered=arms[repetition:]+arms[:repetition]
            for execution,width,hints in ordered:
                yield dict(panel='ring',rows=rows,objects=objects,ops=524291,execution=execution,width=width,hints=hints,mode='throughput',repetition=repetition)
    for repetition in range(2):
        for execution,width,hints in [('direct',1,0),('manual',8,0),('ring',8,0),('ring',32,1)]:
            yield dict(panel='ring',rows=8388608,objects=256,ops=524291,execution=execution,width=width,hints=hints,mode='latency',repetition=repetition)
    for rows in [1024,65536]:
        for batch in [1,64]:
            for age in [0,8]:
                for repetition in range(2):
                    for mode in ['throughput','latency']:
                        yield dict(panel='append',rows=rows,objects=1,ops=513,batch=batch,age=age,mode=mode,repetition=repetition,accounting=0)
                yield dict(panel='append',rows=rows,objects=1,ops=513,batch=batch,age=age,mode='throughput',repetition=0,accounting=1)

def report(samples,out):
    fields=['panel','rows','objects','ops','execution','width','hints','batch','age','mode','repetition','accounting','wall_ns','ns_per_request','caller_cpu_ns','process_cpu_ns','digest','completed','logical_rows','publications','prefix_copied_bytes','requested_body_bytes','tail_encoded_values','release_ns','held_live_capacity','released_live_capacity','held_versions','released_versions','metadata_peak','pool_reuses','driver_record_bytes','ring_arena_charge','returned_quanta','ring_visits']
    with (out/'measurements.csv').open('w') as f:
        writer=csv.DictWriter(f,fields);writer.writeheader()
        for case,data in samples:
            row={k:case.get(k,'') for k in fields}
            if case['panel']=='ring' and case['execution']=='fused':
                phase=data['interval']; row.update(wall_ns=phase['wall_ns'],caller_cpu_ns=phase['caller_cpu_ns'],process_cpu_ns=phase['process_cpu_ns'],completed=phase['logical_requests'],logical_rows=phase['logical_rows'])
                row['digest']=data['outcome_digest']
            else:
                row.update({k:data.get(k,'') for k in fields if k not in case})
                if case['panel']=='append':
                    row.update(held_live_capacity=data['held']['live_capacity_bytes'],released_live_capacity=data['released']['live_capacity_bytes'],held_versions=data['held']['versions'],released_versions=data['released']['versions'],metadata_peak=data['released']['metadata_peak'],pool_reuses=data['released']['pool_reuses'])
            row['ns_per_request']=float(row['wall_ns'])/case['ops'] if not case.get('accounting') else ''
            writer.writerow(row)
    rows=list(csv.DictReader((out/'measurements.csv').open()));groups={}
    keys=['panel','rows','objects','ops','execution','width','hints','batch','age','mode','accounting']
    for row in rows:groups.setdefault(tuple(row[k] for k in keys),[]).append(row)
    with (out/'summary.csv').open('w') as f:
        writer=csv.DictWriter(f,keys+['observations','ns_per_request_median','ns_per_request_observations']);writer.writeheader()
        for key,values in groups.items():
            times=[float(v['ns_per_request']) for v in values if v['ns_per_request']]
            writer.writerow(dict(zip(keys,key))|dict(observations=len(values),ns_per_request_median=statistics.median(times) if times else '',ns_per_request_observations=json.dumps(times)))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);parser.add_argument('--cpu',type=int,default=int(os.environ.get('SIXDB_CPU','0')));parser.add_argument('--jobs',type=int,default=1);parser.add_argument('--report-only',type=Path);parser.add_argument('--pilot',action='store_true');args=parser.parse_args()
    if args.report_only:
        run_data=json.loads((args.report_only/'samples.json').read_text());report(run_data,args.output);return
    config=dict(cpu=args.cpu,jobs=args.jobs,profile='growth-and-ring-first',repetitions=1 if args.pilot else 2,pilot=args.pilot,march='x86-64-v3',tune='zen5',pmu='off')
    workspace=Path(os.environ.get('SIXDB_DATA_CACHE',ROOT/'build'))/'orbital-broad-feedback'
    run=Run(ROOT,args.output,config,workspace=workspace);err=None
    try:
        build=run.build_dir;source=run.source_root
        run.step('configure',['cmake','-S',source,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=clang++-21','-DSIXDB_SPIKES=orbital-mvcc','-DSIXDB_MARCH=x86-64-v3','-DSIXDB_TUNE=zen5'])
        run.step('build',['cmake','--build',build,'--target','loom_object_ring_performance','orbital_append_performance','orbital_versioned_performance_bench','-j',str(args.jobs)])
        binaries=build/'workbench/spikes/orbital-mvcc/performance'
        identities={n:hashlib.sha256((binaries/n).read_bytes()).hexdigest() for n in ['loom_object_ring_performance','orbital_append_performance','orbital_versioned_performance_bench']}
        (args.output/'binary-identity.json').write_text(json.dumps(identities,indent=2)+'\n')
        samples=[]
        for i,case in enumerate(panel(args.pilot)):
            name=f'trial-{i:03d}';opts=[]
            if case['panel']=='ring':
                fused=case['execution']=='fused';binary='orbital_versioned_performance_bench' if fused else 'loom_object_ring_performance'
                opts=['--rows',str(case['rows']),'--objects',str(case['objects']),'--ops',str(case['ops']),'--mode',case['mode']]
                if fused:opts+=['--backend','composed-inline','--workload','dependent-16','--batch','1','--codec','shape','--conditioning','warm-trace','--bytes',str(1024**3),'--metadata-bytes',str(64*1024**2)]
                else:opts+=['--execution',case['execution'],'--width',str(case['width']),'--hints',str(case['hints']),'--group','1024','--steps','16']
            else:
                binary='orbital_append_performance';opts=['--rows',str(case['rows']),'--ops',str(case['ops']),'--batch',str(case['batch']),'--retention-age',str(case['age']),'--mode',case['mode'],'--accounting',str(case['accounting'])]
            if case['mode']=='latency':opts+=['--latency-samples',str(args.output/(name+'-latency.csv'))]
            run.step(name,['taskset','-c',str(args.cpu),'/usr/bin/time','-f','peak_rss_kib=%M',str(binaries/binary),*opts],stdout_name=name+'.json')
            data=json.loads((args.output/(name+'.json')).read_text());assert data['passed']
            samples.append((case,data));(args.output/'samples.json').write_text(json.dumps(samples,indent=2)+'\n')
        # Actual staged pairs must have equal terminal outputs and admission/refill boundaries.
        for case,data in samples:
            if case.get('execution')!='manual':continue
            pair=[d for c,d in samples if c==case|{'execution':'ring'}]
            assert len(pair)==1,(case,len(pair))
            for key in ['digest','completed','logical_rows','replacement_admissions','returned_quanta']:assert data[key]==pair[0][key],(case,key)
        for case,data in samples:
            if case['panel']=='ring':
                matches=[d for c,d in samples if c['panel']=='ring' and c['rows']==case['rows'] and c['objects']==case['objects']]
                assert all(d.get('outcome_digest',d.get('digest'))==data.get('outcome_digest',data.get('digest')) for d in matches)
        report(samples,args.output)
        (args.output/'qualification.json').write_text(json.dumps({'passed':True,'trials':len(samples),'performance_trials':sum(not c.get('accounting') for c,d in samples),'accounting_replays':sum(c.get('accounting',0) for c,d in samples),'independent_oracle':'map lookup for reads, row vector plus count/prefix/tail checks and independent copy/codec formulas for append','manual_ring_refill_parity':True,'sanitizer_claim':False},indent=2)+'\n')
        (args.output/'build-cost.json').write_text(json.dumps([c for c in run.receipt['commands'] if c['name'] in ['configure','build']],indent=2)+'\n')
        run.compact(['measurements.csv','summary.csv','qualification.json','binary-identity.json','build-cost.json'],[])
    except Exception as e:err=e
    err=run.finish(err)
    if err:raise err
if __name__=='__main__':main()
