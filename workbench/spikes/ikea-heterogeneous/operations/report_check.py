#!/usr/bin/env python3
"""Regenerate supplied captures offline and reject corrupted work accounting.

Usage: python3 report_check.py RAW_OR_COMPACT_CAPTURE [...]
All writes go to temporary directories under ignored build/experiments.
"""
import contextlib
import copy
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile

import report

OUTPUTS = {'algebra-selected.csv', 'algebra-summary.csv', 'algebra-summary.md',
           'analyser-summary.csv', 'analyser-summary.md'}


def regenerate(source, destination):
    names = OUTPUTS | {'algebra-timings.csv', 'analyser-timings.csv', 'run.json', 'provenance.json', 'artifact.json'}
    if (source / 'provenance.json').exists():
        names |= json.loads((source / 'provenance.json').read_text())['files_sha256'].keys()
    for name in names:
        if (source / name).exists():
            (destination / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / name, destination / name)
    if (source / 'source.tar.gz').exists():
        (destination / 'source.tar.gz').symlink_to((source / 'source.tar.gz').resolve())
    before = {name: (source / name).read_bytes() for name in OUTPUTS if (source / name).exists()}
    with contextlib.redirect_stdout(io.StringIO()):
        report.main(destination)
    if any((destination / name).read_bytes() != data for name, data in before.items()):
        raise RuntimeError(f'Regeneration changed selected evidence bytes: {source}')


def change_first(key, value):
    return lambda rows: rows[0].__setitem__(key, str(value(rows[0]) if callable(value) else value))


def bad_available_pmu(key, value):
    def mutate(rows):
        row = rows[0]
        # A quick run may not request PMU counters. Construct an available-row
        # control before corrupting its selected count, rather than treating a
        # legitimate not_requested/zero-count row as an error.
        row['pmu_status'] = 'available'
        for field in ('cycles_raw', 'instructions_raw', 'enabled_ns', 'running_ns'):
            if int(row[field]) == 0: row[field] = '1'
        row[key] = str(value(row) if callable(value) else value)
    return mutate


def remove_tail(rows):
    keys = ('dataset', 'mask', 'operation', 'configuration', 'policy')
    key = tuple(rows[0][field] for field in keys)
    choices = [index for index, row in enumerate(rows) if tuple(row[field] for field in keys) == key]
    del rows[max(choices, key=lambda index: int(rows[index]['repetition']))]


def wrong_single_count(rows):
    for row in rows:
        if row['mask'] == 'single255': row['selected_slices'] = str(2 * int(row['calls']))


def impossible_plain_work(rows):
    for row in rows:
        if row['configuration'] == 'plain' and row['mask'] == 'all':
            row['plain_loads'] = '0'
            row['metadata_frames'] = row['calls']


def unstable_body_work(rows):
    for row in rows:
        if row['mask'] == 'all' and row['configuration'] == 'local-frame' and row['repetition'] == '0':
            # Keep divisibility and bounds intact; only repetition consistency fails.
            row['bec_decodes'] = str(int(row['bec_decodes']) - int(row['passes']))
            return
    raise RuntimeError('No case available for body-work mutation')


def inconsistent_windows(rows):
    row = rows[0]
    row['windows'] = str(2 * int(row['windows']))
    row['calls'] = str(2 * int(row['calls']))
    row['ns_per_call'] = str(int(row['elapsed_ns']) / int(row['calls']))


CASES = [
    ('algebra', 'calls', change_first('calls', lambda r: int(r['calls']) + 1)),
    ('algebra', 'checksum', change_first('checksum', lambda r: int(r['checksum']) + 1)),
    ('algebra', 'selected count', change_first('selected_slices', 1)),
    ('algebra', 'duplicate repetition', change_first('repetition', 1)),
    ('algebra', 'PMU running', bad_available_pmu('running_ns', lambda r: int(r['enabled_ns']) + 1)),
    ('algebra', 'PMU cycles', bad_available_pmu('cycles_raw', 0)),
    ('algebra', 'output extent', change_first('output_bytes', lambda r: int(r['output_bytes']) - 32)),
    ('algebra', 'missing tail repetition', remove_tail),
    ('algebra', 'missing configuration', lambda rows: rows.__setitem__(slice(None), [r for r in rows if r['configuration'] != 'local-frame'])),
    ('algebra', 'fixed mask cardinality', wrong_single_count),
    ('algebra', 'plain accounting', impossible_plain_work),
    ('algebra', 'unstable body accounting', unstable_body_work),
    ('algebra', 'input extent', change_first('left_bytes', -1)),
    ('algebra', 'unknown PMU status', change_first('pmu_status', 'unknown-status')),
    ('analyser', 'calls', change_first('calls', lambda r: int(r['calls']) + 1)),
    ('analyser', 'checksum', change_first('checksum', lambda r: int(r['checksum']) + 1)),
    ('analyser', 'prediction conversions', change_first('conversions', 1)),
    ('analyser', 'PMU running', bad_available_pmu('running_ns', lambda r: int(r['enabled_ns']) + 1)),
    ('analyser', 'inconsistent windows', inconsistent_windows),
    ('analyser', 'unknown PMU status', change_first('pmu_status', 'unknown-status')),
]


def main(sources):
    home = report.ROOT / 'build/experiments/ikea-heterogeneous-operations-report-check'
    home.mkdir(parents=True, exist_ok=True)
    for source in map(Path, sources):
        with tempfile.TemporaryDirectory(dir=home) as temporary:
            directory = Path(temporary)
            regenerated = directory / 'regenerated'; regenerated.mkdir()
            regenerate(source, regenerated)
            count = 0
            for part, label, mutate in CASES:
                name = 'algebra-selected.csv' if part == 'algebra' else 'analyser-timings.csv'
                if not (regenerated / name).exists(): continue
                rows = report.read(regenerated / name)
                dataset = 'structural' if any(r['dataset'] == 'structural' for r in rows) else rows[0]['dataset']
                rows = copy.deepcopy([r for r in rows if r['dataset'] == dataset])
                # Give quick captures two independent repeats for mutations that
                # specifically test a missing/inconsistent repeat. No timings
                # derived from this constructed control are retained as evidence.
                if {r['repetition'] for r in rows} == {'0'}:
                    rows += [dict(r, repetition='1') for r in rows]
                mutate(rows)
                candidate = directory / 'candidate'; candidate.mkdir(exist_ok=True)
                report.write(candidate / name, rows)
                try:
                    with contextlib.redirect_stdout(io.StringIO()):
                        (report.algebra if part == 'algebra' else report.analyser)(candidate)
                except ValueError:
                    count += 1
                else:
                    raise RuntimeError(f'Accepted invalid {part} {label}: {source}')
            print(f'{source}: byte-identical regeneration; {count} invalid inputs rejected')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    main(sys.argv[1:])
