#!/usr/bin/env python3
"""Complete uniform-fanout controls, separate from the original mixed short bags."""
import argparse, csv, importlib.util, json, random, subprocess, time
from pathlib import Path
p=argparse.ArgumentParser(__doc__)
p.add_argument('binary',type=Path);p.add_argument('output',type=Path)
p.add_argument('--reps',type=int,default=9);p.add_argument('--cpu',default='1')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('report',Path(__file__).with_name('formats-report.py'))
report=importlib.util.module_from_spec(spec);spec.loader.exec_module(report)
for fanout in (1,2,3,4,8,32,128):
    cases=[(k,arm,large) for k in (16,40,64) for arm in range(8) for large in (0,1)]
    random.Random(127+fanout).shuffle(cases);receipts=[]
    csv_path=a.output/f'fanout-{fanout}.csv'
    with csv_path.open('w') as f:
        writer=None
        for k,arm,large in cases:
            command=['taskset','-c',a.cpu,str(a.binary),'run',str(k),'2',str(arm),str(a.reps),str(large),str(fanout)]
            start=time.time_ns();run=subprocess.run(command,check=True,text=True,stdout=subprocess.PIPE);end=time.time_ns()
            rows=list(csv.DictReader(run.stdout.splitlines()))
            if writer is None:writer=csv.DictWriter(f,fieldnames=rows[0]);writer.writeheader()
            writer.writerows(rows);f.flush();receipts.append(dict(command=command,start_ns=start,end_ns=end))
    (a.output/f'fanout-{fanout}-processes.json').write_text(json.dumps(receipts,indent=2)+'\n')
    result=report.summarize(csv_path,inline=True)
    (a.output/f'fanout-{fanout}-summary.json').write_text(json.dumps(result,indent=2)+'\n')
