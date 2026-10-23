#!/usr/bin/env python3
"""Keep fixed-iteration Bec timings and whole-process perf diagnostics distinct."""
import argparse
import csv
import importlib.util
import json
from pathlib import Path

SPEC = importlib.util.spec_from_file_location('cohesion_results', Path(__file__).with_name('cohesion-v2-results.py'))
shared = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(shared)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', help='member=results directory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    sources, records = {}, []
    for item in args.inputs:
        label, directory = item.split('=', 1)
        root = Path(directory).resolve()
        receipt = json.loads((root / 'receipt.json').read_text())
        assert label not in sources and receipt['trials'] == 5
        sources[label] = {'receipt_sha256': shared.sha(root / 'receipt.json'),
                          'source_manifest_sha256': receipt['source_manifest_sha256'],
                          'binaries': receipt['binaries'], 'controls': receipt['controls'], 'files_sha256': {}}
        for profile in receipt['profiles']:
            for mode in receipt['modes']:
                for case in receipt['cases']:
                    metrics = {}
                    for arm in ['before', 'after']:
                        for trial in range(5):
                            stem = case.replace('/', '-') + f'-{mode}-{trial}'
                            path = root / profile / arm / (stem + '.json')
                            sources[label]['files_sha256'][str(path.relative_to(root))] = shared.sha(path)
                            rows = [r for r in json.loads(path.read_text())['benchmarks'] if r.get('run_type', 'iteration') == 'iteration']
                            assert len(rows) == 1 and rows[0]['name'] == case
                            row = rows[0]
                            assert row['iterations'] == receipt['iterations'] and row['time_unit'] == 'ns'
                            for name in ['cpu_time', 'real_time']:
                                metrics.setdefault('benchmark_' + name + '_ns', {'before': {}, 'after': {}})[arm][trial] = row[name]
                            if receipt['perf_command']:
                                path = root / profile / arm / (stem + '-perf.csv')
                                sources[label]['files_sha256'][str(path.relative_to(root))] = shared.sha(path)
                                with path.open(newline='') as handle:
                                    for counter in csv.reader(s for s in handle if s.strip() and not s.startswith('#')):
                                        # perf may scale counts. Compare only observations
                                        # it reports at 100.00% running; keep the raw CSV.
                                        group = metrics.setdefault('process_' + counter[2], {'before': {}, 'after': {}})
                                        if counter[0].isdigit() and len(counter) > 4 and float(counter[4]) >= 100:
                                            group[arm][trial] = int(counter[0])
                    for metric, arms in sorted(metrics.items()):
                        if any(set(values) != set(range(5)) for values in arms.values()):
                            records.append({'member': label, 'profile': profile, 'mode': mode, 'case': case,
                                            'metric': metric, 'status': 'incomplete_or_not_fully_running', 'raw_samples': arms})
                            continue
                        before, after = [[arms[arm][i] for i in range(5)] for arm in ['before', 'after']]
                        left, right = shared.stats(before), shared.stats(after)
                        records.append({'member': label, 'profile': profile, 'mode': mode, 'case': case,
                                        'metric': metric, 'status': 'complete', 'before': left, 'after': right,
                                        'after_over_before': shared.stats([b / a for a, b in zip(before, after)]) if all(before) else None,
                                        'ratio_of_medians': right['median'] / left['median'] if left['median'] else None})
    (args.output / 'paired-cases.json').write_text(json.dumps(records, indent=2) + '\n')
    provenance = {'sources': sources, 'reader_sha256': shared.sha(Path(__file__)),
                  'records': len(records), 'paired_cases_sha256': shared.sha(args.output / 'paired-cases.json'),
                  'limits': 'Diagnostic with fixed five million benchmark iterations and common argv[0], with ASLR control where available. Timed source is unchanged. Perf counts the entire user-space process, including startup/fixtures/framework; these are not exact loop instruction counts. Only events perf reports at 100.00% running are compared. Perf default scaling and reporting precision remain in its raw CSV; this reader applies no further scaling. Generic cache misses are diagnostic and do not establish a residency level. Benchmark timing and process counters remain separate; no pooled speedup or confidence interval is inferred.'}
    (args.output / 'provenance.json').write_text(json.dumps(provenance, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'output': str(args.output), 'records': len(records)}, indent=2))


if __name__ == '__main__':
    main()
