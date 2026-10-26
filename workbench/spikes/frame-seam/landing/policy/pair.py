import json, os
from pathlib import Path
import subprocess, sys
root=Path(sys.argv[1]); arms=['qualified','first','final']; answers={}
for round in range(5):
    for arm in arms[round%3:]+arms[:round%3]:
        p=subprocess.run(['taskset','-c',os.environ['SIXDB_CPU'],str(root/arm),arm],capture_output=True,text=True,check=True)
        rows=[json.loads(s) for s in p.stdout.splitlines()]
        keys={(x['density'],x['first'],x['op']) for x in rows}
        if len(rows)!=52 or len(keys)!=52: raise RuntimeError('incomplete policy cohort')
        for row in rows:
            key=(row['density'],row['first'],row['op']); answer=(row['hi'],row['lo'])
            if key in answers and answers[key]!=answer: raise RuntimeError(f'wrong answer {key}')
            answers[key]=answer;row['round']=round;print(json.dumps(row),flush=True)
