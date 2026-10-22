#!/usr/bin/env python3
"""Select the separate domain-sized native comparison; no historical pooling."""
import argparse
import csv
import json
from pathlib import Path
import statistics

INPUTS=['OS-ResolutionWidth','Region-ConnectTiming','IMDb-year','functional','decorrelated']

def write_csv(path,rows):
    fields=list(dict.fromkeys(k for row in rows for k in row))
    with path.open('w',newline='') as f:
        out=csv.DictWriter(f,fields);out.writeheader();out.writerows(rows)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input',type=Path);ap.add_argument('output',type=Path)
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    queries=[];materials=[];repairs=[];calibration=[];domains=[];checks=0
    for source in INPUTS:
        raw=json.loads((args.input/(source+'.json')).read_text())
        checks+=raw[-1]['comparisons']
        indexed={}
        for r in raw:
            if r['type']=='material':materials.append({'input':source,**{k:v for k,v in r.items() if k!='type'}})
            elif r['type']=='native-domain':domains.append({'input':source,**{k:v for k,v in r.items() if k!='type'}})
            elif r['type']=='query':
                samples=r['whole_query_warm_ns'];assert len(samples)==26
                item={'input':source,**{k:v for k,v in r.items() if k not in ('type','whole_query_warm_ns')},
                      'median_ns':statistics.median(samples),'min_ns':min(samples),'max_ns':max(samples),'samples':len(samples)}
                queries.append(item);indexed[r['query'],r['operation'],r['arm']]=(r,item)
            elif r['type']=='repair-construction':
                times=r['whole_part_repair_and_retire_ns'];assert len(times)==6
                repairs.append({'input':source,**{k:v for k,v in r.items() if k not in ('type','whole_part_repair_and_retire_ns')},
                                'median_ns':statistics.median(times),'min_ns':min(times),'max_ns':max(times),'samples':len(times)})
        keys={(q,op) for q,op,_ in indexed}
        for q,op in sorted(keys):
            values=[r['repeats_per_sample'] for (qq,oo,_),(r,_) in indexed.items() if (qq,oo)==(q,op)]
            assert len(set(values))==1,'batch mismatch'
        for q in sorted({q for q,_,_ in indexed}):
            pairs=[('independent-plain','independent-plain-duplicate','grouped-COUNT-SUM'),
                   ('independent-plain','independent-plain-duplicate','isolated-X-predicated-SUM'),
                   ('shared-pair','shared-pair-decode-X','grouped-COUNT-SUM'),
                   ('native-pair-fused','native-pair-coalesced','isolated-X-predicated-SUM'),
                   ('native-shared-fused','native-shared-coalesced','isolated-X-predicated-SUM'),
                   ('native-lanes-flat','native-lanes-coalesced','isolated-X-predicated-SUM'),
                   ('native-global-FOR-flat','native-global-FOR-coalesced','isolated-X-predicated-SUM')]
            for a,b,op in pairs:
                ar,am=indexed[q,op,a];br,bm=indexed[q,op,b]
                calibration.append({'input':source,'query':q,'operation':op,'first':a,'second':b,
                    'first_median_ns':am['median_ns'],'second_median_ns':bm['median_ns'],
                    'median_ratio':bm['median_ns']/am['median_ns'],
                    'median_absolute_percent_difference':abs(bm['median_ns']/am['median_ns']-1)*100,
                    'paired_ratio_median':statistics.median(b/a for a,b in zip(ar['whole_query_warm_ns'],br['whole_query_warm_ns']))})
    write_csv(args.output/'domains.csv',domains)
    write_csv(args.output/'queries.csv',queries);write_csv(args.output/'material.csv',materials)
    write_csv(args.output/'repairs.csv',repairs);write_csv(args.output/'calibration.csv',calibration)
    exact=[r for r in calibration if r['first']=='independent-plain']
    result={'reported_logical_comparisons':checks,'query_rows':len(queries),'raw_query_samples':sum(r['samples'] for r in queries),
            'private_repair_rows':len(repairs),'exact_duplicate_median_absolute_percent_difference':statistics.median(r['median_absolute_percent_difference'] for r in exact),
            'exact_duplicate_maximum_absolute_percent_difference':max(r['median_absolute_percent_difference'] for r in exact),
            'selection':'all five inputs, all queries and measured programs, both operations; all parts for repair controls; duplicate/order calibration; raw samples in recovery archive'}
    (args.output/'coverage.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))

if __name__=='__main__':main()
