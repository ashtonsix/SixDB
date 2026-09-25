#!/usr/bin/env python3
"""Per-block local timestamp-boundary diagnostics beside the flow comparisons."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'oneway'))
from analyze import Clock,read_events,quantile,write_csv


def main():
    p=argparse.ArgumentParser();p.add_argument('campaign',type=Path);p.add_argument('--inputs',type=Path)
    p.add_argument('--clock-point-model',choices=('affine','feasible'),default='affine');args=p.parse_args()
    refs=json.loads((args.campaign/'workers.json').read_text())
    rows=[]
    for name,member in refs['members'].items():
        folder=args.inputs/name if args.inputs else ROOT/'build/workers'/member['job']/'results'
        identity=json.loads((folder/'identity.json').read_text());clock=Clock(folder/'calibration.jsonl',point_model=args.clock_point_model)
        for case in json.loads((folder/'cases.json').read_text()):
            values={'hardware_to_software_rx':[],'software_to_app_rx':[],'app_to_software_tx':[]}
            for event in read_events(folder/case['file']):
                if event['seq']<20:continue
                raw,point,_,_=clock.stamp(event)
                if event['tx']:
                    values['app_to_software_tx'].append((raw-event['app_raw'])/1000)
                else:
                    values['software_to_app_rx'].append((event['app_raw']-raw)/1000)
                    if event['hw']:
                        values['hardware_to_software_rx'].append(((clock.origin-event['hw'])+raw+point)/1000)
            for boundary,v in values.items():
                if v:
                    rows.append(dict(node=identity['node'],host=name.removeprefix('use1-'),src=case['src'],dst=case['dst'],
                                     repeat=case['repeat'],round=case['round'],flow=case['flow'],boundary=boundary,n=len(v),
                                     p50_us=quantile(v,.5),p99_us=quantile(v,.99),min_us=min(v),max_us=max(v)))
    write_csv(args.campaign/'summary'/'boundary-blocks.csv',rows)
    print(f'{len(rows)} per-block boundary diagnostics; PHC mapping remains model-dependent')


if __name__=='__main__':main()
