#!/usr/bin/env python3
"""Summarize retained ENA counters around the actual throughput experiment phases."""
import argparse,json,re,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from summarize import write_csv


def counters(document):
    return {k:int(v) for k,v in re.findall(r'^\s*([\w]+):\s*(\d+)\s*$',document['counters']['stdout'],re.M)}


def analyze(inputs,output):
    rows=[];context=[]
    for worker,folder in sorted(inputs.items()):
        initial=json.loads((folder/'initial.json').read_text())
        context.append(dict(worker=worker,host=json.loads((folder/'host.json').read_text()),
                            network={k:initial[k] for k in ['link','driver','offloads','rings','channels','coalescing','tcp-settings','byte_queue_limits'] if k in initial},
                            socket_tuning=json.loads((folder/'socket-tuning.json').read_text())))
        for before in sorted(folder.glob('*-before.json')):
            after=before.with_name(before.name.replace('-before.json','-after.json'))
            if not after.exists():continue
            a=json.loads(before.read_text());b=json.loads(after.read_text())
            if 'counters' not in a or 'counters' not in b:continue
            x,y=counters(a),counters(b)
            row=dict(worker=worker,phase=before.name.removesuffix('-before.json'),seconds=b['utc']-a['utc'],ena_srd_mode=y.get('ena_srd_mode','unavailable'))
            for key in ['bw_in_allowance_exceeded','bw_out_allowance_exceeded','pps_allowance_exceeded','conntrack_allowance_exceeded','linklocal_allowance_exceeded','ena_srd_eligible_tx_pkts','ena_srd_tx_pkts','ena_srd_rx_pkts']:
                row[key]=y[key]-x[key] if key in x and key in y else ''
            eligible=row['ena_srd_eligible_tx_pkts'];sent=row['ena_srd_tx_pkts']
            row['srd_sent_of_eligible_pct']=100*sent/eligible if isinstance(eligible,int) and eligible>0 and isinstance(sent,int) else ''
            rows.append(row)
    output.mkdir(parents=True,exist_ok=True);write_csv(output/'network-counters.csv',rows)
    (output/'network-context.json').write_text(json.dumps(context,indent=2)+'\n')
    print(json.dumps(dict(network_intervals=len(rows))))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',nargs='+');p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    analyze({name:Path(path) for name,path in [v.split('=',1) for v in a.input]},a.output)
