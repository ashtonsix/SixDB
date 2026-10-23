#!/usr/bin/env python3
"""Retain every direct-binding preparation case and paired native repetition."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', help='member=collected results directory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    all_rows, provenance = [], {}
    for named in args.inputs:
        member, directory = named.split('=', 1)
        root = Path(directory)
        receipt_path = root/'receipt.json'
        receipt = json.loads(receipt_path.read_text())
        assert receipt['complete']
        groups = {}
        files = {}
        for trial in receipt['trials']:
            if trial.get('family') == 'series':
                continue
            original = Path(trial['path'])
            path = root/trial['profile']/original.name
            assert digest(path) == trial['sha256']
            files[str(path)] = digest(path)
            rows = [r for r in json.loads(path.read_text())['benchmarks'] if r.get('run_type') == 'iteration']
            assert len(rows) == trial['cases'] == 27
            expected_route = 'shared_plan' if trial['route'] == 'duplicate' else trial['route']
            for row in rows:
                assert not row.get('error_occurred') and row['time_unit'] == 'ns' and row['blocks'] == 64
                prefix, route, *tail = row['name'].split('/')
                assert prefix == 'prepare' and route == expected_route
                count = int(tail[-1].removeprefix('q'))
                assert count in (1, 8, 64) and row['queries_per_command'] == count
                assert abs(row['cpu_time']/64-row['ns_per_command']) < max(1e-7, row['ns_per_command']*1e-10)
                key = (trial['profile'], '/'.join(tail))
                entry = groups.setdefault(key, {})
                per_route = entry.setdefault(trial['route'], {})
                assert trial['trial'] not in per_route
                per_route[trial['trial']] = row['cpu_time']/64
        assert len(groups) == len(receipt['profiles'])*27
        for (profile, case), routes in sorted(groups.items()):
            assert set(routes) == {'construct_plan', 'direct', 'shared_plan', 'duplicate'}
            assert all(set(samples) == set(range(5)) for samples in routes.values())
            samples = {route: [values[t] for t in range(5)] for route,values in routes.items()}
            row = {'member': member, 'profile': profile, 'case': case, 'metric': 'cpu_ns_per_command'}
            for route, values in samples.items():
                row[route+'_samples'] = json.dumps(values)
                row[route+'_median'] = statistics.median(values)
                row[route+'_min'] = min(values); row[route+'_max'] = max(values)
            for numerator, denominator in [('direct','construct_plan'), ('direct','shared_plan'),
                                            ('duplicate','shared_plan')]:
                name = numerator+'_over_'+denominator
                values = [a/b for a,b in zip(samples[numerator], samples[denominator])]
                row[name+'_samples'] = json.dumps(values)
                row[name+'_median'] = statistics.median(values)
            all_rows.append(row)
        provenance[member] = {'receipt_sha256': digest(receipt_path), 'trials_sha256': files,
                              'predicate_header_sha256': receipt['predicate_header_sha256'],
                              'orders': receipt['orders'], 'profiles': receipt['profiles']}
    with (args.output/'cases.csv').open('w', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=list(all_rows[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(all_rows)
    summary = {}
    for profile in sorted({r['profile'] for r in all_rows}):
        rows = [r for r in all_rows if r['profile'] == profile]
        assert len(rows) == 27
        summary[profile] = {}
        for metric in ('direct_over_construct_plan', 'direct_over_shared_plan', 'duplicate_over_shared_plan'):
            ratios = [r[metric+'_median'] for r in rows]
            summary[profile][metric] = {'range': [min(ratios), max(ratios)],
                                       'wins_5pct': sum(v <= .95 for v in ratios),
                                       'losses_5pct': sum(v >= 1.05 for v in ratios)}
    result = {'scope': __doc__, 'units': 'CPU ns per complete block command; all attached-view admission/preparation and q queries included.',
              'cases': len(all_rows), 'summary': summary, 'sources': provenance,
              'cases_sha256': digest(args.output/'cases.csv'),
              'limits': 'Warm resident fixture, validated layouts, no layout validation/allocation/cold caches timed; no duplicate correction or comparison to old f153 cohorts.'}
    (args.output/'summary.json').write_text(json.dumps(result, indent=2, sort_keys=True)+'\n')
    print(json.dumps({'cases': len(all_rows), 'summary': summary}, indent=2))


if __name__ == '__main__':
    main()
