#!/usr/bin/env python3
"""Retain observed D3 request semantics separately from untraced timing passes."""
import argparse
import csv
import json
from pathlib import Path
import re
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from summarize import stats,write_csv


def analyze(folder,output):
    output.mkdir(parents=True,exist_ok=True)
    device=json.loads((folder/'before.json').read_text())
    device_name=Path(device['device']).name
    device_number=json.loads(device['lsblk']['stdout'])['blockdevices'][0]['maj:min'].replace(':',',')
    grouped={};passes=[];traces=[]
    for row in json.loads((folder/'diagnostic-cases.json').read_text()):
        raw=list(csv.DictReader((folder/row['file']).open()))
        assert len(raw)==row['samples']
        data={key:[int(v[key])/1000 for v in raw] for key in ['latency_ns','write_completion_ns','sync_remainder_ns']}
        key=row['layout'],row['method'];entry=grouped.setdefault(key,{column:[] for column in data})
        for column,values in data.items():entry[column]+=values
        passes.append(row|stats(data['latency_ns']))
    pooled=[]
    for (layout,method),data in sorted(grouped.items()):
        pooled.append({'layout':layout,'method':method,**stats(data['latency_ns']),
                       **{'write_'+k:v for k,v in stats(data['write_completion_ns']).items()},
                       **{'sync_'+k:v for k,v in stats(data['sync_remainder_ns']).items()}})
        source=folder/f'{layout}-{method}-trace.txt';lines=source.read_text().splitlines()
        # The old trace-cmd formatter cannot render every Linux 7 NVMe payload.
        # Preserve its raw byte arrays and use independently decoded block flags too.
        commands=[]
        for index,line in enumerate(lines):
            if 'nvme_setup_cmd:' not in line or f'disk={device_name} ' not in line:continue
            m=re.search(r'opcode=(\d+).*cdw10=ARRAY\[([^]]+)\]',line)
            if not m:continue
            data=bytes(int(v.strip(),16) for v in m.group(2).split(','))
            commands.append({'line':index+1,'opcode':int(m.group(1)),
                             'lba':int.from_bytes(data[:8],'little'),'cdw12':int.from_bytes(data[8:12],'little')})
        blocks=[]
        for index,line in enumerate(lines):
            m=re.search(r'block_rq_issue:\s+'+re.escape(device_number)+r' (\S+) (\d+) \(\) (\d+) \+ (\d+)',line)
            if m:blocks.append({'line':index+1,'flags':m.group(1),'bytes':int(m.group(2)),
                               'sector':int(m.group(3)),'sectors':int(m.group(4))})
        data_writes=[b for b in blocks if b['bytes']==4096 and b['flags'].startswith('W') and 'M' not in b['flags']]
        assert len(data_writes)==42,(source,len(data_writes))
        start=data_writes[0]['line'];end=next((b['line'] for b in blocks if b['line']>data_writes[-1]['line'] and b['flags'].startswith('R')),len(lines)+1)
        decoded=[c for c in commands if c['opcode']==1 and c['cdw12']&65535==7 and c['lba'] in {b['sector'] for b in data_writes}]
        traces.append({'layout':layout,'method':method,'trace_file':source.name,'data_writes':len(data_writes),
                       'block_fua_writes':sum('F' in b['flags'] for b in data_writes),
                       'decoded_nvme_writes':len(decoded),'decoded_nvme_fua_writes':sum(bool(c['cdw12']&(1<<30)) for c in decoded),
                       'flushes_after_first_data_write_before_recovery':sum(b['flags']=='FF' and start<b['line']<end for b in blocks),
                       'first_data_write_line':start,'first_recovery_read_line':end})
    write_csv(output/'d3-diagnostic.csv',pooled);write_csv(output/'d3-diagnostic-passes.csv',passes)
    write_csv(output/'d3-request-traces.csv',traces)
    print(json.dumps({'timing_passes':len(passes),'untraced_samples':sum(p['samples'] for p in passes),'traced_methods':len(traces)}))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('input',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();analyze(args.input,args.output)
