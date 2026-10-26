#!/usr/bin/env python3
"""Compare original, qualified span, and landed code with rotating process rounds."""
import collections
import json
import os
from pathlib import Path
import subprocess
import sys

build, out, rounds = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
root = build / 'workbench/spikes/frame-seam'
arms = [('base_current', root / 'frame_seam_base_span', 'current'),
        ('qualified_span', root / 'frame_seam_qualified_span', 'span'),
        ('candidate_current', root / 'frame_seam_span_compare', 'current'),
        ('candidate_span', root / 'frame_seam_span_compare', 'span')]
images = collections.defaultdict(set)
counts = collections.Counter()
with out.open('w') as output:
    for r in range(rounds):
        for arm, exe, method in arms[r % len(arms):] + arms[:r % len(arms)]:
            process = subprocess.run(
                ['taskset', '-c', os.environ['SIXDB_CPU'], str(exe), arm, method, '1'],
                check=True, text=True, capture_output=True)
            rows = [json.loads(line) for line in process.stdout.splitlines()]
            if len(rows) != 216 or len({(x['column'], x['op']) for x in rows}) != 216:
                raise RuntimeError(f'{arm} round {r}: incomplete or duplicate cohort')
            for row in rows:
                row['round'] = r
                images[row['column']].add((row['image_bytes'], row['image_hash']))
                counts[arm] += 1
                output.write(json.dumps(row) + '\n')
            output.flush()
        print(f'span round {r + 1}/{rounds}', flush=True)
if any(len(v) != 1 for v in images.values()) or len(images) != 24:
    raise RuntimeError('Frame images differ between arms')
if dict(counts) != {arm: rounds * 216 for arm, _, _ in arms}:
    raise RuntimeError(f'incomplete arms: {counts}')
print('complete, image-matched:', dict(counts), flush=True)
