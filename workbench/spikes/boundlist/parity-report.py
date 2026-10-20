#!/usr/bin/env python3
"""Render absolute StreamPack before/after slices and retain every paired cell."""
import csv
import pathlib
import sys

directory = pathlib.Path(sys.argv[1])

def records(name):
    return list(csv.DictReader((directory / (name + '.csv')).open()))
def pairs(rows):
    ignored = {'implementation', 'repetitions', 'median_ns', 'min_ns', 'max_ns'}
    grouped = {}
    for r in rows:
        key = tuple((k, v) for k, v in r.items() if k not in ignored)
        grouped.setdefault(key, {})[r['implementation']] = r
    for values in grouped.values():
        before = values.get('before_streampack', values.get('before'))
        current = values.get('current_streampack', values.get('production'))
        if not before or not current:
            assert len(values) == 1 and 'independent_fused' in values
            continue
        assert before['repetitions'] == current['repetitions'] and int(before['repetitions']) in (5, 7)
        yield before, current, values.get('independent_fused')

def number(row):
    return float(row['median_ns'])
read_pairs, mutation_pairs, packing_pairs = [list(pairs(records(n))) for n in ('reads', 'mutation', 'packing')]
lines = ['# StreamPack extraction parity', '',
         'Absolute medians from five counterbalanced reader/mutation repetitions and seven packing repetitions. Reader and mutation ns are per value/command; packing ns are per 256-rank block. C1 is dependent serial work; C8 reader/mutation values are reciprocal throughput per request. Packing B8 is an independent stream of eight complete blocks, not per-slot interleaving.', '',
         '## Complete readers', '',
         'The slice uses W16, grouped R/U, variable geometry with used lengths. Each cell is before → current; independent fused controls are retained in `reads.csv`.', '',
         '| Host/ISA | Blocks | Geometry | C1 ns/value | C8 ns/value | Ordered ns/value | Shuffled ns/value | Carried ns/value |',
         '| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |']
selected = {}
for before, current, fused in read_pairs:
    if before['maximum'] == '4095' and before['used'] == '1' and before['fixed'] == '0' and before['placement'] == '0':
        key = (before['host'], before['isa'], int(before['rows']) // 256, before['layout'])
        selected.setdefault(key, {})[before['metric']] = f'{number(before):.2f} → {number(current):.2f}'
for (host, isa, blocks, layout), values in sorted(selected.items()):
    metrics = ('dependent_value_chain', 'independent8_value_chain', 'ordered_projection', 'shuffled_projection', 'carried_range')
    lines.append(f'| {host}/{isa} | {blocks} | {layout} | '+' | '.join(values[m] for m in metrics)+' |')
lines += ['', '## Mutations with source-qualified effects', '',
          'Width/W16, grouped R/U. Append periodically resets the same first eight blocks; only replace spreads over the larger fixture. The clock includes the useful value consumer and the actual journal.', '',
          '| Host/ISA | Allocated blocks | Operation | C1 ns/command | C8 ns/command |',
          '| --- | ---: | --- | ---: | ---: |']
selected = {}
for before, current, _ in mutation_pairs:
    if before['layout'] == 'width' and before['maximum'] == '4095' and before['used'] == '1':
        key = (before['host'], before['isa'], int(before['nodes']), before['metric'])
        selected.setdefault(key, {})[before['chains']] = f'{number(before):.2f} → {number(current):.2f}'
for (host, isa, nodes, metric), values in sorted(selected.items()):
    lines.append(f'| {host}/{isa} | {nodes} | {metric} | {values["1"]} | {values["8"]} |')
lines += ['', '## Actual soft packing', '',
          'W16, grouped R/U, shrink enabled, every fifth rank retired, live U varies 8–20 and R is 32. Each trial restores the fragmented input outside its clock; both treatments use the same aligned journal storage. The selected loop captures the immutable assigned count before stores. Journal storage and its complete digest are included; independent byte/geometry checks run outside the clock. Clock-pair overhead is included. This is a conditioned block operation, not a cold-memory benchmark.', '',
          '| Host/ISA | Geometry | Serial ns/block | B8 stream ns/block |',
          '| --- | --- | ---: | ---: |']
selected = {}
for before, current, _ in packing_pairs:
    if before['maximum'] == '4095' and before['used'] == '1' and before['placement'] == '0' and before['shrink'] == '1':
        key = (before['host'], before['isa'], before['layout'])
        selected.setdefault(key, {})[before['blocks']] = f'{number(before):.2f} → {number(current):.2f}'
for (host, isa, layout), values in sorted(selected.items()):
    lines.append(f'| {host}/{isa} | {layout} | {values["1"]} | {values["8"]} |')
lines += ['', '## Full-surface differences', '',
          'These are descriptive extrema across every paired case, not a pooled timing or a significance test. Positive delta means current is slower. Units remain distinct between families.', '',
          '| Family | Host/ISA | Cells | Median ratio current/before | Largest ns loss | Largest ns gain |',
          '| --- | --- | ---: | ---: | ---: | ---: |']
import statistics
for family, cells in (('read ns/value', read_pairs), ('mutation ns/command', mutation_pairs), ('packing ns/block', packing_pairs)):
    for host, isa in sorted({(b['host'], b['isa']) for b, _, _ in cells}):
        cohort = [(b, c) for b, c, _ in cells if b['host'] == host and b['isa'] == isa]
        deltas = [number(c)-number(b) for b,c in cohort]
        lines.append(f'| {family} | {host}/{isa} | {len(cohort)} | {statistics.median(number(c)/number(b) for b,c in cohort):.3f} | {max(deltas):.2f} | {min(deltas):.2f} |')
lines += ['', 'The full matrices preserve minimum/maximum and all repetitions by recoverable raw input. They include Direct/Width, W8/W16, optional U, grouped/separated placement, fixed/variable readers, and ordered/shuffled access. No new stored fields, allocation, count capacity or wire-format changes accompany extraction. Typed access is still 16 B; the stored schema is still 8 B.', '',
          'Point/projection instruction comparison matches all 180 paired complete consumers after relocation/padding normalization. Large x86 timing swings in both directions therefore do not establish a runtime cost of the extraction seam; the raw ranges remain visible. Carried traversal is a distinct composition and has small remaining deltas. `packing_initial.csv` preserves the first experiment, before shared output placement and the count capture. It is not silently replaced by the follow-up.', '',
          'Hardware receipts and process RSS are in the worker archives. Read fixtures report logical payload, allocated capacity, metadata and bindings separately. Large random reads here do not by themselves establish per-load DRAM service. Search, reverse and actual Loom comparisons remain in the separately retained graduation evidence.', '']
(directory / 'scorecard.md').write_text('\n'.join(lines))
print(f'paired cells: reads={len(read_pairs)} mutations={len(mutation_pairs)} packing={len(packing_pairs)}')
