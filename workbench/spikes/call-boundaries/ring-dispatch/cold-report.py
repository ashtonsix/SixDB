#!/usr/bin/env python3
"""Recompute six-factor medians from hashed selected inputs, without timing."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
import cold_analysis

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def check(condition,message):
    if not condition:raise ValueError(message)

def report(root):
    index=json.loads((root/'recovery.json').read_text())
    # Verify every source/audit/excerpt/qualification and sample input before
    # deriving reports. Analysis code comes from this checkout, never the
    # retained input bundle.
    paths=set()
    for member in index['payloads']:
        path=root/member['path'];check(member['path'] not in paths,'duplicate selected path');paths.add(member['path'])
        check(path.is_file() and path.stat().st_size==member['bytes'] and sha(path)==member['sha256'],f'changed selected input {path}')
    check('tools/analyze.py' in paths,'unindexed historical analyzer')
    native=vars(cold_analysis)
    arms=native['ARMS'];modes=native['MODES']
    all_cells=[];total=0
    for job_id,job in index['jobs'].items():
        directory=root/'workers'/job_id
        order=list(csv.DictReader((directory/'executable-order.csv').open()));entries={}
        for row in order:
            isa,n,chains,passed=row['isa'],int(row['n']),int(row['chains']),int(row['pass'])
            policy=row['policy'];mode,grain=policy.removeprefix('cold_').split('_grain');grain=int(grain)
            check(mode in modes and passed in (0,1) and row['reps']=='7' and row['window_mode']=='1','invalid execution identity')
            key=(isa,n,chains,passed,mode,grain);check(key not in entries,'duplicate cell')
            name=f'{isa}-{policy}-{n}-{chains}-pass{passed}'
            ctx=native['context'](directory/(name+'.context'))
            data=native['rows'](directory/(name+'.csv'),ctx,n,chains,grain,7)
            hardware=json.loads((directory/f'hardware-{isa}.json').read_text())
            check(hardware['behavior_context']==ctx['hardware_context'] and not hardware['calibration_enabled'],'hardware context changed')
            entries[key]={'rows':data,'context':ctx,'order':int(row['order'])};total+=len(data)
        for isa in job['isas']:
            required={(isa,n,c,p,m,g) for n in (128,4194304) for c in (1,32) for p in (0,1) for m in modes for g in (1,4)}
            check({k for k in entries if k[0]==isa}==required,'missing or extra selected cells')
            for n in (128,4194304):
                for chains in (1,32):
                    selected=[v for k,v in entries.items() if k[:3]==(isa,n,chains)]
                    contexts={tuple(v['context'][f] for f in ('hardware_context','calibration_enabled','policy_revision','pressure','line_bytes','page_bytes','demand_code','selected_hint')) for v in selected}
                    check(len(contexts)==1,'factor policy context differs')
                    populations={tuple(int(r[f]) for f in ('packed_requested_bytes','direct_requested_bytes','abseil_requested_bytes','key_input_bytes','primary_keys','secondary_keys','overflow_keys')) for v in selected for r in v['rows']}
                    check(len(populations)==1,'factor physical work differs')
                    for passed in (0,1):
                        sequence=[(m,g) for g in (1,4) for m in modes]
                        if passed:sequence.reverse()
                        check([entries[(isa,n,chains,passed,m,g)]['order'] for m,g in sequence]==list(range(12)),'factor pass order')
                    for grain in (1,4):
                        medians={}
                        for mode in modes:
                            medians[mode]={str(p):{a:statistics.median(float(r['ns_per_lookup']) for r in entries[(isa,n,chains,p,mode,grain)]['rows'] if r['execution']==a) for a in arms} for p in (0,1)}
                            medians[mode]['merged']={a:statistics.median(float(r['ns_per_lookup']) for p in (0,1) for r in entries[(isa,n,chains,p,mode,grain)]['rows'] if r['execution']==a) for a in arms}
                        comparisons={}
                        for newer,older in (('sync','reference'),('tail','sync'),('vector','tail'),('snapshot','vector'),('slim','tail')):
                            ratios={p:{a:medians[newer][p][a]/medians[older][p][a] for a in ('ring','ring-hints')} for p in ('0','1','merged')}
                            ratios['pass1_over_pass0_ratio']={a:ratios['1'][a]/ratios['0'][a] for a in ('ring','ring-hints')}
                            ratios['direction_drift_flag']=any(abs(v-1)>.05 for v in ratios['pass1_over_pass0_ratio'].values())
                            comparisons[newer+'_over_'+older]=ratios
                        manual={m:{p:{'unhinted':medians[m][p]['ring']/medians[m][p]['manual'],'hinted':medians[m][p]['ring-hints']/medians[m][p]['manual-hints']} for p in ('0','1','merged')} for m in modes}
                        all_cells.append({'job_id':job_id,'isa':isa,'n':n,'chains':chains,'grain':grain,'independent_checksum':native['checksum'](n),
                            'physical_population_and_bytes':list(populations)[0],'medians_ns':medians,'factor_ratios':comparisons,'ring_over_actual_manual':manual})
    check(total==index['counts']['qualified_rows'],'selected row count changed')
    result={'status':'independently recomputed from every verified selected input','source_commit':index['source_commit'],'source_digest':index['source_digest'],
        'report_sha256':sha(Path(__file__)),'analysis_sha256':sha(Path(cold_analysis.__file__)),
        'indexed_payloads_verified':len(paths),'recovery_sha256':sha(root/'recovery.json'),
        'rows':total,'cells':all_cells,'limits':index['limits']}
    destination=root/'reports';destination.mkdir(exist_ok=True)
    (destination/'cells.json').write_text(json.dumps(result,indent=2)+'\n')
    with (destination/'endpoint-medians.csv').open('w') as stream:
        fields=['job_id','isa','n','chains','grain','mode','pass','ring_ns','ring_hints_ns','manual_ns','manual_hints_ns','ring_over_manual','ring_hints_over_manual_hints']
        writer=csv.DictWriter(stream,fieldnames=fields);writer.writeheader()
        for cell in all_cells:
            for mode,passes in cell['medians_ns'].items():
                for passed,values in passes.items():
                    writer.writerow({k:cell[k] for k in fields[:5]}|{'mode':mode,'pass':passed,'ring_ns':values['ring'],'ring_hints_ns':values['ring-hints'],
                        'manual_ns':values['manual'],'manual_hints_ns':values['manual-hints'],'ring_over_manual':values['ring']/values['manual'],'ring_hints_over_manual_hints':values['ring-hints']/values['manual-hints']})
    print(f"verified {len(paths)} selected payloads; recomputed {total} rows/{len(all_cells)} cells; {destination}")
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--evidence',type=Path,required=True)
    report(parser.parse_args().evidence.resolve())
