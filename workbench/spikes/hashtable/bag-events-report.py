#!/usr/bin/env python3
"""Qualify untimed observer inventories against the matching timed histories."""
import argparse
import csv
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
timed = list(csv.DictReader((a.directory/'values.csv').open()))
phases = []
identities = {}
matches = 0
for width in (2, 8, 16):
    source = a.directory/f'events-{width}.jsonl'
    records = [json.loads(line) for line in source.read_text().splitlines()]
    assert len(records) == 12
    identities[source.name] = hashlib.sha256(source.read_bytes()).hexdigest()
    binary = a.directory/f'events-{width}'
    if binary.exists():
        identities[binary.name] = hashlib.sha256(binary.read_bytes()).hexdigest()
    for ordinal, arm in enumerate((1, 5, 6)):
        context, *history = records[ordinal*4:(ordinal+1)*4]
        assert context['record'] == 'context'
        assert tuple(context[f] for f in ('arm', 'K', 'value_bytes', 'A', 'B', 'n', 'budget', 'unit', 'fanout')) == (
            arm, 40, width, 10, 6, 6144, 16, 192, 0)
        assert context['block_slots'] == {1:16, 5:64, 6:256}[arm]
        previous_keys, previous_values = 0, 0
        for phase, event in zip(('build', 'update', 'erase'), history):
            assert event['record'] == 'phase' and event['phase'] == phase
            assert tuple(event[f] for f in ('arm', 'K', 'value_bytes', 'A', 'n', 'kind')) == (arm, 40, width, 10, 6144, 2)
            assert event['keys'] == (3072 if phase == 'erase' else 6144)
            assert event['operations'] == (3072 if phase == 'erase' else 6144)
            assert event['mutation_calls'] == (context['input_values'] if phase == 'build' else event['operations'])
            assert event['caller_events'] == event['mutation_calls']
            assert event['caller_keys_delta'] == event['keys']-previous_keys
            assert event['caller_values_delta'] == event['values']-previous_values
            if phase != 'erase':
                assert event['values'] == context['input_values']+(6144 if phase == 'update' else 0)
            assert sum(event['current_'+f] for f in ('primary_rows', 'secondary_rows', 'overflow_rows')) == event['keys']
            for name in ('bags', 'key_overflow'):
                link = event[name]
                for count, histogram in (('new_blocks', 'blocks_by_family'), ('growths', 'growths_by_family'),
                                         ('block_writes', 'writes_by_family'), ('issued_damage_bytes', 'damage_by_family')):
                    assert len(link[histogram]) == 10 and all(x >= 0 for x in link[histogram])
                    assert sum(link[histogram]) == link[count]
                assert link['new_blocks'] == link['directory_insertions']
            assert event['all_issued_damage_bytes'] == sum(event[f] for f in ('primary_issued_damage_bytes', 'secondary_issued_damage_bytes'))+sum(
                event[name]['issued_damage_bytes'] for name in ('bags', 'key_overflow'))
            same = [r for r in timed if tuple(int(r[f]) for f in ('value_bytes', 'arm', 'A', 'fanout')) == (width, arm, 10, 0) and r['phase'] == phase]
            assert same
            for row in same:
                for field in ('operations', 'requested', 'owned_capacity', 'metadata'):
                    assert int(row[field]) == event[field], (width, arm, phase, field, row[field], event[field])
                if phase == 'build':
                    for tier in ('primary_rows', 'secondary_rows', 'overflow_rows'):
                        assert int(row['build_'+tier]) == event['current_'+tier]
            matches += len(same)
            phases.append(event)
            previous_keys, previous_values = event['keys'], event['values']
assert len(phases) == 27
output = dict(qualification='Untimed logical events; issued damage sums overlap and are not unique bytes, traffic, or MVCC copying.',
              widths=[2,8,16], contexts=9, phases=phases, matching_timed_rows=matches, sha256=identities)
a.output.write_text(json.dumps(output, indent=2)+'\n')
print(f'Validated 9 histories, 27 phase inventories and {matches} matching timed rows')
print('W/arm/phase: bag blocks, growths, issued damage bytes')
for event in phases:
    link = event['bags']
    print(event['value_bytes'], event['arm'], event['phase'], link['new_blocks'], link['growths'], link['issued_damage_bytes'])
