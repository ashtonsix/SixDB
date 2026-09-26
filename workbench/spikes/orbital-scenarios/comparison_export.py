#!/usr/bin/env python3
"""Keep complete cohort outcomes and useful counters from the contention study.

Selects all competitors, widths, seeds and sensitivity contrasts; drops event
traces and redundant payloads. Synthetic ticks are never converted to seconds.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import sys

from comparison import source_identity


def write_csv(path, records):
    fields = list(dict.fromkeys(key for record in records for key in record))
    with path.open('w', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sweep', type=Path, required=True)
    parser.add_argument('--sensitivity', type=Path, required=True)
    parser.add_argument('--extra', action='append', default=[], metavar='NAME=PATH',
                        help='additional output-admission, fixed-position or fold series')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    sources = source_identity()
    runs, cohorts, inputs = [], [], {}
    seen = set()
    series_paths = [('sweep', args.sweep), ('sensitivity', args.sensitivity)]
    for extra in args.extra:
        name, separator, path = extra.partition('=')
        if not name or not separator or not path:
            parser.error('--extra requires NAME=PATH')
        series_paths.append((name, Path(path)))
    assert len({name for name, _ in series_paths}) == len(series_paths)
    for series, path in series_paths:
        payload = json.loads(path.read_text())
        for name, expected in payload['source_sha256'].items():
            actual = hashlib.sha256((Path(__file__).parent / name).read_bytes()).hexdigest()
            assert actual == expected, f'{series}: source changed: {name}'
        inputs[series] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                          **{k: v for k, v in payload.items() if k != 'rows'}}
        for row in payload['rows']:
            assert row['serial_check']
            variant = row.get('variant', row.get('run_kind', 'default'))
            if 'window_ticks' in row:
                variant += f"/window={row['window_ticks']}/cap={row['cap']}"
            identity = [series, variant, row['case'], row['policy'], row['seed'], row['width']]
            run_id = '/'.join(map(str, identity))
            assert run_id not in seen
            seen.add(run_id)
            common = dict(zip(['series', 'variant', 'case', 'policy', 'seed', 'width'], identity))
            record = {**common, 'time_ticks': row['time_ticks'],
                      'input_sha256': row['input_sha256'], 'trace_sha256': row['trace_sha256'],
                      'logical_state_sha256': row['logical_state_sha256'],
                      'serial_check': row['serial_check'],
                      'changes': json.dumps(row.get('changes', row.get('overrides', {})), sort_keys=True)}
            for key in ('horizon', 'cap', 'window_ticks', 'original_input_sha256',
                        'group_manifest_sha256', 'original_serial_sha256',
                        'original_serial_check', 'original_requests', 'execution_groups',
                        'multi_member_groups', 'max_group_size', 'committed_original_requests',
                        'grouping_scope'):
                if key in row:
                    record[key] = row[key]
            if 'original_serial_check' in row:
                assert row['original_serial_check']
            for group in ('counts', 'arbitration', 'certification', 'write_admission'):
                for key, value in row.get(group, {}).items():
                    record[f'{group}.{key}'] = value
            record['retained_versions_no_gc'] = row['retained_versions_no_gc']
            runs.append(record)
            for group, values in row['cohorts'].items():
                assert values['complete'] + values['failed'] + values['pending'] == values['offered']
                cohorts.append({**common, 'cohort': group,
                    **{k: v for k, v in values.items() if k != 'rejections'},
                    'rejections': json.dumps(values.get('rejections', {}), sort_keys=True)})
    args.output.mkdir(parents=True, exist_ok=True)
    write_csv(args.output / 'runs.csv', runs)
    write_csv(args.output / 'cohorts.csv', cohorts)
    provenance = {'source_sha256': sources, 'inputs': inputs,
                  'exporter_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  'run_count': len(runs), 'cohort_count': len(cohorts),
                  'series_paths': {name: str(path) for name, path in series_paths},
                  'export_command': ['python3', 'workbench/spikes/orbital-scenarios/comparison_export.py',
                                     *sys.argv[1:]],
                  'selection': 'All runs and cohort outcomes, including failures and pending work. '
                               'No event traces, per-attempt payloads or measured wall-clock times.',
                  'commands': [
                      'python3 workbench/spikes/orbital-scenarios/comparison.py --output build/orbital-scenarios/comparison-sweep.json --seeds 0,7,19 --widths 16,64,256 --quiet',
                      'python3 workbench/spikes/orbital-scenarios/comparison_study.py --output build/orbital-scenarios/comparison-sensitivity.json',
                      'python3 workbench/spikes/orbital-scenarios/comparison_export.py --sweep build/orbital-scenarios/comparison-sweep.json --sensitivity build/orbital-scenarios/comparison-sensitivity.json --output build/orbital-scenarios/contention-evidence'],
                  'linux_prefix_from_macos': 'orb -m ubuntu'}
    (args.output / 'study.json').write_text(json.dumps(provenance, indent=2, sort_keys=True) + '\n')
    print(f'Exported {len(runs)} runs and {len(cohorts)} cohort outcomes to {args.output}')


if __name__ == '__main__':
    main()
