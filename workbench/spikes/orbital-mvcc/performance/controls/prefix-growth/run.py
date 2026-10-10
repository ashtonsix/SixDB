#!/usr/bin/env python3
"""Small matched append alternative; explicit reservation, no LMDB arm."""
import argparse,csv,hashlib,json,os,sys,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parents[6]
sys.path.insert(0,str(ROOT/'workbench/tools'))
from experiment import Run

def report(samples,out):
    with (out/'measurements.csv').open('w') as f:
        writer=csv.writer(f);writer.writerow(['backend','mode','repetition','accounting','rows','ops','batch','age','reserve_bytes','wall_ns','ns_per_append','caller_cpu_ns','prefix_copied_bytes','held_live_capacity','released_live_capacity','held_versions','metadata_peak','mapped_bytes','reserved_bytes','digest'])
        for c,d in samples:
            writer.writerow([c['backend'],c['mode'],c['repetition'],c['accounting'],65536,513,1,0,c['reservation'],d['wall_ns'],d['wall_ns']/513 if d['timing_valid'] else '',d['caller_cpu_ns'],d['prefix_copied_bytes'],d['held']['live_capacity_bytes'],d['released']['live_capacity_bytes'],d['held']['versions'],d['released']['metadata_peak'],d['released']['pool_mapped_bytes'],d['released'].get('pool_reserved_bytes',''),d['digest']])
    rows=list(csv.DictReader((out/'measurements.csv').open()));groups={}
    for r in rows:groups.setdefault((r['backend'],r['mode'],r['accounting']),[]).append(r)
    with (out/'summary.csv').open('w') as f:
        w=csv.writer(f);w.writerow(['backend','mode','accounting','observations','ns_per_append_median','observations_ns'])
        for key,rs in groups.items():
            times=[float(r['ns_per_append']) for r in rs if r['ns_per_append']];w.writerow([*key,len(rs),statistics.median(times) if times else '',json.dumps(times)])

def main():
    a=argparse.ArgumentParser();a.add_argument('--output',type=Path,required=True);a.add_argument('--report-only',type=Path);o=a.parse_args()
    if o.report_only:report(json.loads((o.report_only/'samples.json').read_text()),o.output);return
    run=Run(ROOT,o.output,{'case':'65536 prefix/513 single-row appends, original reader held','reservation_bytes':2097152,'order':'AB/BA within each mode','pmu':'off'},workspace=Path(os.environ['SIXDB_DATA_CACHE'])/'orbital-broad-feedback');err=None
    try:
        run.step('configure',['cmake','-S',run.source_root,'-B',run.build_dir,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=clang++-21','-DSIXDB_SPIKES=orbital-mvcc','-DSIXDB_MARCH=x86-64-v3','-DSIXDB_TUNE=zen5'])
        run.step('build',['cmake','--build',run.build_dir,'--target','orbital_prefix_growth_bench','orbital_prefix_growth_check','-j','1'])
        run.step('native-check',['ctest','--test-dir',run.build_dir,'-R','^orbital_prefix_growth_check$','--output-on-failure'])
        executable=run.build_dir/'workbench/spikes/orbital-mvcc/performance/orbital_prefix_growth_bench'
        (o.output/'binary-identity.json').write_text(json.dumps({'sha256':hashlib.sha256(executable.read_bytes()).hexdigest()},indent=2)+'\n')
        cases=[]
        for mode in ['throughput','latency']:
            for repetition in range(2):
                for backend in (['fresh-copy','shared-prefix'] if repetition==0 else ['shared-prefix','fresh-copy']):cases.append(dict(backend=backend,reservation=2097152 if backend=='shared-prefix' else 0,mode=mode,repetition=repetition,accounting=0))
        cases += [dict(backend=b,reservation=2097152 if b=='shared-prefix' else 0,mode='throughput',repetition=0,accounting=1) for b in ['fresh-copy','shared-prefix']]
        samples=[]
        for i,c in enumerate(cases):
            args=['--backend',c['backend'],'--rows','65536','--ops','513','--batch','1','--retention-age','0','--mode',c['mode'],'--accounting',str(c['accounting'])]
            if c['reservation']:args+=['--reserve-bytes',str(c['reservation'])]
            if c['accounting']:args+=['--verify-only','1']
            if c['mode']=='latency':args+=['--latency-samples',str(o.output/f'trial-{i:02d}-latency.csv')]
            run.step(f'trial-{i:02d}',['taskset','-c',os.environ['SIXDB_CPU'],str(executable),*args],stdout_name=f'trial-{i:02d}.json')
            data=json.loads((o.output/f'trial-{i:02d}.json').read_text());assert data['passed'];samples.append((c,data));(o.output/'samples.json').write_text(json.dumps(samples,indent=2)+'\n')
        assert len({d['digest'] for c,d in samples})==1
        assert all(d['final_count']==66049 and d['released']['versions']==1 and d['released']['contexts']==0 for c,d in samples)
        report(samples,o.output)
        (o.output/'qualification.json').write_text(json.dumps({'passed':True,'trials':10,'performance_trials':8,'clock_free_accounting':2,'oracle_digests_equal':True,'selected':False,'sanitizer_claim':False},indent=2)+'\n')
        run.compact(['measurements.csv','summary.csv','samples.json','qualification.json','binary-identity.json'],[])
    except Exception as e:err=e
    err=run.finish(err)
    if err:raise err
if __name__=='__main__':main()
