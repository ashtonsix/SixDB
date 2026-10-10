#!/usr/bin/env python3
"""Logical replay of the frozen survey generator; never executes database code."""
from array import array
from pathlib import Path
import hashlib
import json

OUT = Path(__file__).resolve().parent
N = 32768
Q = 524291
CORPUS_SEED = 8132
TRACE_SEED = CORPUS_SEED + 1
OBJECT_SEED_STRIDE = 109
MASK = (1 << 64) - 1
C = 0x9e3779b97f4a7c15
ROW_BYTES = 23
LINE_BYTES = 64


def mix(z):
    z = ((z ^ (z >> 30)) * 0xbf58476d1ce4e5b9) & MASK
    z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) & MASK
    return z ^ (z >> 31)


# The two selected non-YCSB workloads consume five Random::next calls per
# Operation: key, unused proposed next, value, tag, and percent. Only the first
# determines the starting key. key_at(rank) = rank*32+17, so retain ranks here.
starts = array('I', (mix((TRACE_SEED + C * (5 * i + 1)) & MASK) % N for i in range(Q)))
perms = []
ranks = []
for obj in range(256):
    p = array('I', range(N))
    state = CORPUS_SEED + obj * OBJECT_SEED_STRIDE
    for i in range(N, 1, -1):
        state = (state + C) & MASK
        j = mix(state) % i
        p[i - 1], p[j] = p[j], p[i - 1]
    inv = array('I', [0]) * N
    for i, row in enumerate(p):
        inv[row] = i
    perms.append(p)
    ranks.append(inv)


def coverage(objects, kind):
    total_rows = total_lines = completed = 0
    per = []
    for obj in range(objects):
        diff = array('i', [0]) * (N + 1)
        for i in range(obj, Q, objects):
            first = ranks[obj][starts[i]] if kind == 'dependent-16' else starts[i]
            end = first + 16
            diff[first] += 1
            if end <= N:
                diff[end] -= 1
                completed += 16
            elif kind == 'dependent-16':
                diff[N] -= 1
                diff[0] += 1
                diff[end - N] -= 1
                completed += 16
            else:
                diff[N] -= 1
                completed += N - first
        seen = bytearray(N)
        level = 0
        for rank in range(N):
            level += diff[rank]
            if level:
                seen[perms[obj][rank] if kind == 'dependent-16' else rank] = 1
        # MODELED full-row span exposure, NOT disassembled load spans or
        # observed hardware cache-line accesses/service. Rows are shorter than
        # a line, so first and last line cover the complete [23*r,23*r+22] span.
        lines = bytearray(N * ROW_BYTES // LINE_BYTES)
        for row, yes in enumerate(seen):
            if yes:
                lines[row * ROW_BYTES // LINE_BYTES] = 1
                lines[(row * ROW_BYTES + ROW_BYTES - 1) // LINE_BYTES] = 1
        count = sum(seen)
        total_rows += count
        total_lines += sum(lines)
        per.append(count)
    return {
        'workload': kind,
        'objects': objects,
        'rows_per_object': N,
        'requests': Q,
        'matched_rows_including_revisits': completed,
        'distinct_matched_rows': total_rows,
        'distinct_row_fraction': total_rows / (N * objects),
        'per_object_distinct_rows_min_max': [min(per), max(per)],
        'modeled_matched_row_span_64B_lines': total_lines,
        'modeled_body_64B_lines': N * ROW_BYTES // LINE_BYTES * objects,
        'modeled_matched_row_span_line_fraction': total_lines / (N * ROW_BYTES // LINE_BYTES * objects),
        'body_bytes': N * ROW_BYTES * objects,
    }


results = [coverage(objects, 'dependent-16') for objects in (1, 64, 256)]
results += [coverage(objects, 'scan-16') for objects in (1, 256)]
# Pin the reviewed deterministic results, independently of later generator edits.
assert [x['distinct_matched_rows'] for x in results] == [32768, 2058377, 5304992, 32768, 5301663]
assert [x['modeled_matched_row_span_64B_lines'] for x in results] == [11776, 753664, 2935069, 11776, 2079395]
result = {
    'method': 'Pure integer reproduction of starting ranks and shuffled per-Object cycles; interval union on each cycle, no database execution or clocks.',
    'parameters': {
        'corpus_seed': CORPUS_SEED, 'trace_seed': TRACE_SEED,
        'object_seed_stride': OBJECT_SEED_STRIDE, 'requests': Q,
        'rows_per_object': N, 'chain_steps_or_scan_limit': 16,
        'row_bytes': ROW_BYTES, 'modeled_line_bytes': LINE_BYTES,
        'modeled_body_base_modulo_line': 0,
        'object_assignment': 'request_index % objects',
    },
    'interpretation': 'Line exposure counts complete 23-byte spans of distinct matched rows in one pass. It does not extract actual compiled load spans or observe hardware cache lines serviced. It excludes binary-search probe spans, metadata, input/output streams, speculative loads and prefetch. Repeating warm-trace reuses this same set.',
    'limitations': 'Unique span exposure does not establish reuse distance, cache residency, cache misses, DRAM service, bandwidth or latency. The aligned-base model corresponds to the page-aligned large-Window construction path but records no worker addresses.',
    'results': results,
}
(OUT / 'output.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(results, indent=2))
