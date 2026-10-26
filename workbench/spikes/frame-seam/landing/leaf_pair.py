#!/usr/bin/env python3
"""Pair the old/repaired exact leaf, checking every result and cohort."""
import json
import os
from pathlib import Path
import subprocess
import sys

root, out, rounds = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
answers = {}
expected = None
with out.open('w') as output:
    for r in range(rounds):
        arms = ('old', 'current') if r % 2 == 0 else ('current', 'old')
        for arm in arms:
            run = subprocess.run(['taskset', '-c', os.environ['SIXDB_CPU'],
                                  str(root / ('frame_seam_leaf_' + arm)), arm],
                                 check=True, text=True, capture_output=True)
            rows = [json.loads(line) for line in run.stdout.splitlines()]
            keys = {(x['K'], x['op'], x['density']) for x in rows}
            if expected is None:
                expected = keys
            if keys != expected or len(rows) != len(keys) or len(rows) < 40:
                raise RuntimeError(f'incomplete leaf cohort {arm} round {r}')
            for row in rows:
                key = (row['K'], row['op'], row['density'])
                answer = (row['result_hi'], row['result_lo'])
                if key in answers and answers[key] != answer:
                    raise RuntimeError(f'leaf answer changed: {key}')
                answers[key] = answer
                row['round'] = r
                output.write(json.dumps(row) + '\n')
            output.flush()
        print(f'leaf round {r + 1}/{rounds}', flush=True)
print(f'complete: {len(answers)} checked cells per arm', flush=True)
