#!/usr/bin/env python3
"""Keep losing cells and exact duplicates beside the useful comparisons."""
from pathlib import Path
import argparse,json,statistics

p=argparse.ArgumentParser();p.add_argument('queries',type=Path);a=p.parse_args()
summary=json.loads((a.queries/'summary.json').read_text())
setups=json.loads((a.queries/'setup.json').read_text())
keys=['input','rows','layout','operation','selection','condition']
cells={}
for row in summary:cells.setdefault(tuple(row[k] for k in keys),{})[row['arm']]=row
comparisons=[];duplicates=[]
for key,arms in sorted(cells.items()):
    cell=dict(zip(keys,key));frame=arms['frame']['median_ns']
    controls={name:row['median_ns'] for name,row in arms.items() if 'duplicate' not in name and name not in ['frame','frame-catalogue']}
    comparisons.append(dict(**cell,frame_ns=frame,controls_ns=controls,
        frame_catalogue_ns=arms['frame-catalogue']['median_ns'] if 'frame-catalogue' in arms else None,
        frame_over_control={name:frame/value for name,value in controls.items()}))
    pairs=[('frame','frame-duplicate'),('polars-for','polars-for-duplicate'),
           ('frame-catalogue','frame-catalogue-duplicate'),
           ('polars-prepared-spans','polars-span-duplicate')]
    for left,right in pairs:
        if left in arms and right in arms:
            duplicates.append(dict(**cell,original=left,duplicate=right,
                ratio=arms[right]['median_ns']/arms[left]['median_ns']))
(a.queries/'comparisons.json').write_text(json.dumps(comparisons,indent=2)+'\n')
(a.queries/'duplicate-controls.json').write_text(json.dumps(duplicates,indent=2)+'\n')
ratios=[row['ratio'] for row in duplicates]
print('Exact duplicate ratios: cells',len(ratios),'min',min(ratios),'median',statistics.median(ratios),'max',max(ratios))
largest=max(row['rows'] for row in summary)
for operation,selection,layout in [('count-ge','all','original'),('sum','all','original'),
    ('sum','one-sixteenth-blocks','original'),('sum','one-sixteenth-blocks','block-scattered')]:
    print('\n',operation,selection,layout,'rows',largest,'(complete call microseconds)')
    for row in comparisons:
        if (row['rows'],row['operation'],row['selection'],row['layout'])!=(largest,operation,selection,layout):continue
        controls=row['controls_ns']
        logical=[v for k,v in controls.items() if k in ['polars-series','polars-lazy','polars-gather','polars-prepared-spans','polars-prepared-spans-lazy']]
        for_controls=[v for k,v in controls.items() if k in ['polars-for','polars-for-lazy']]
        print(row['input'],row['condition'],'Frame',round(row['frame_ns']/1000,2),
            'catalogue',round(row['frame_catalogue_ns']/1000,2) if row['frame_catalogue_ns'] is not None else '-',
            'nativeFOR',round(controls['native-for']/1000,2),'Polars-best',round(min(logical)/1000,2),
            'PolarsFOR',round(min(for_controls)/1000,2) if for_controls else '-',
            'span-request',round(controls['polars-span-request']/1000,2) if 'polars-span-request' in controls else '-')
print('\nLargest input isolated capacities and prepared selection costs:')
for row in setups:
    if row['rows']==largest:
        s=row['stats']
        print(row['input'],row['layout'],'Frame',s['frame_capacity'],'nativeFOR',s['native_capacity'],
            'Polars',row['polars_buffer_bytes'],'PolarsFOR',row['polars_for_buffer_bytes'],
            'span-chunks',row['polars_prepared_span_chunks'],'span-bind-us',row['polars_span_bind_ns']/1000)
