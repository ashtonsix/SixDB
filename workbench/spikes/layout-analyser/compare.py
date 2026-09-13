#!/usr/bin/env python3
"""Reproduce selected native contrasts; keep matched seeds and contexts separate."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parent


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    inputs = {}
    contrasts = []
    frontiers = []

    def read(path):
        inputs[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
        with path.open() as f:
            return list(csv.DictReader(f))

    def record(host, family, context, change, seed, baseline, candidate):
        a, b = float(baseline['median_ns']), float(candidate['median_ns'])
        assert a > 0 and b > 0
        contrasts.append(dict(host=host, family=family, context=context, change=change,
            seed=seed, baseline_ns=a, candidate_ns=b, candidate_over_baseline=b/a,
            baseline_allocated_bytes=baseline['allocated_bytes'],
            candidate_allocated_bytes=candidate['allocated_bytes']))

    prefix_pairs = [
        ('D65', 'E64prefix7', '7-byte prefix'),
        ('A63', 'B64prefix9', '9-byte prefix'),
        ('A63', 'C64pad', '63 to 64 padding'),
        ('D65', 'F64side', 'rare byte plane'),
        ('A63', 'A_prefix_plane', 'separate prefix'),
    ]
    bucket_pairs = [
        ('n8b7stride63_combined', 'n8b7stride64_combined', 'N8 padding'),
        ('n8b7stride64_combined', 'n8b8stride64_combined', 'N8 fingerprint 7 to 8'),
        ('n9b7stride76_combined', 'n9b7stride96_combined', 'N9 padding'),
        ('n9b7stride76_combined', 'n9b7stride62_split', 'N9 split'),
        ('n16b8stride120_combined', 'n16b8stride128_combined', 'N16 padding'),
        ('n16b8stride128_combined', 'n16b12stride128_combined', 'N16 fingerprint 8 to 12'),
        ('n16b8stride120_combined', 'n16b8stride104_split', 'N16 split'),
        ('n16b8stride104_split', 'n16b12stride104_split', 'N16 split fingerprint 8 to 12'),
    ]
    for host in ['zen5', 'gnr']:
        rows = read(ROOT / f'evidence/consumers-{host}/summary.csv')
        lookup = {(r['rows'], r['seed'], r['case']): r for r in rows}
        assert len(lookup) == len(rows)
        # Both recipes retain ordinary TuplePack; omit raw byte-only reads.
        pool = [r for r in rows if r['family'] == 'bucket' and r['rows'] == '262144'
                and '/load90_uniform/mixed/' in r['case'] and not r['case'].endswith('/raw')]
        points = []
        for name in sorted({r['case'] for r in pool}):
            observations = [r for r in pool if r['case'] == name]
            assert len(observations) == 2
            allocations = {int(r['allocated_bytes']) for r in observations}
            assert len(allocations) == 1  # query seeds vary, the table is fixed
            points.append(dict(host=host, case=name,
                median_seed_median_ns=median(float(r['median_ns']) for r in observations),
                allocated_bytes=allocations.pop()))
        for point in points:
            dominated = any(q['median_seed_median_ns'] <= point['median_seed_median_ns']
                and q['allocated_bytes'] <= point['allocated_bytes']
                and (q['median_seed_median_ns'] < point['median_seed_median_ns']
                     or q['allocated_bytes'] < point['allocated_bytes']) for q in points)
            frontiers.append(point | {'on_observed_frontier': not dominated})
        for size in ['4096', '262144']:
            for seed in ['712367', '918273']:
                for a, b, label in prefix_pairs:
                    for op in ['all_fields', 'scalar_tuplepack', 'full_string_rare',
                               'equality_mismatch7', 'equality_mismatch8',
                               'equality_mismatch17', 'update_tuplepack_effects']:
                        record(host, 'prefix', f'rows{size}/{op}', label, seed,
                               lookup[size, seed, f'{a}/{op}'], lookup[size, seed, f'{b}/{op}'])
                for load in ['load50_uniform', 'load90_uniform', 'load90_skew']:
                    for a, b, label in bucket_pairs:
                        for recipe in ['rawfp_tuplepack', 'seriespack_tuplepack']:
                            for op in ['hit', 'miss', 'mixed', 'update']:
                                ka, kb = f'{a}/{load}/{op}/{recipe}', f'{b}/{load}/{op}/{recipe}'
                                if (size, seed, ka) not in lookup or (size, seed, kb) not in lookup:
                                    continue  # unsupported ordinary placement, not a failed timing
                                record(host, 'bucket', f'rows{size}/{load}/{op}/{recipe}',
                                       label, seed, lookup[size, seed, ka], lookup[size, seed, kb])

    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / 'consumer-ratios.csv').open('w') as f:
        w = csv.DictWriter(f, fieldnames=list(contrasts[0])); w.writeheader(); w.writerows(contrasts)
    with (args.output / 'bucket-frontier.csv').open('w') as f:
        w = csv.DictWriter(f, fieldnames=list(frontiers[0])); w.writeheader(); w.writerows(frontiers)
    lines = ['# Selected paired consumer contrasts', '',
        'Candidate / baseline elapsed time. Below 1 is faster. Ranges are the two matched seed ratios of three-repetition medians, not confidence intervals. All rows use 262,144 logical records/keys. The CSV also retains the 4,096-row controls and other operations.', '',
        '| Change / operation | Zen 5 ratio range | Granite Rapids ratio range |',
        '| --- | ---: | ---: |']
    for family, context, change in [
        ('prefix', 'rows262144/equality_mismatch7', '7-byte prefix'),
        ('prefix', 'rows262144/equality_mismatch8', '9-byte prefix'),
        ('prefix', 'rows262144/equality_mismatch17', '7-byte prefix'),
        ('prefix', 'rows262144/all_fields', '63 to 64 padding'),
        ('prefix', 'rows262144/equality_mismatch7', 'separate prefix'),
        ('prefix', 'rows262144/full_string_rare', 'separate prefix'),
        *[('bucket', 'rows262144/load90_uniform/mixed/rawfp_tuplepack', name)
          for name in ['N8 padding', 'N8 fingerprint 7 to 8', 'N9 padding', 'N9 split',
                       'N16 padding', 'N16 fingerprint 8 to 12', 'N16 split']],
        ('bucket', 'rows262144/load90_uniform/mixed/seriespack_tuplepack',
         'N16 split fingerprint 8 to 12'),
    ]:
        cells = []
        for host in ['zen5', 'gnr']:
            selected = [r['candidate_over_baseline'] for r in contrasts
                        if (r['host'], r['family'], r['context'], r['change']) ==
                           (host, family, context, change)]
            assert len(selected) == 2
            cells.append(f'{min(selected):.3f}–{max(selected):.3f}')
        lines.append(f'| {change} / {context.split("/", 1)[1]} | ' + ' | '.join(cells) + ' |')
    lines += ['', 'Candidate definitions and recipe differences are in `prefix/README.md` and `bucket/README.md`. This selection illustrates contrasts; complete retained samples include every measured candidate and repetition.']
    lines += ['', '## Authored space/time frontier', '',
        'Same 262,144-key, 90%-uniform mixed read trace; both recipes retain ordinary TuplePack. Median of seed medians, encoded buffer footprint only. This is the observed finite-menu frontier, not statistical dominance. `bucket-frontier.csv` includes every compared point.', '',
        '| Host | Candidate / recipe | Encoded bytes | ns/op |',
        '| --- | --- | ---: | ---: |']
    for point in frontiers:
        if point['on_observed_frontier']:
            parts = point['case'].split('/')
            lines.append(f'| {point["host"]} | {parts[0]} / {parts[-1]} | '
                         f'{point["allocated_bytes"]:,} | {point["median_seed_median_ns"]:.3f} |')
    (args.output / 'summary.md').write_text('\n'.join(lines) + '\n')
    (args.output / 'provenance.json').write_text(json.dumps({
        'inputs_sha256': inputs, 'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'kind': 'post-hoc paired contrasts of retained elapsed timings; no new native measurements',
        'ratio': 'candidate median ns / baseline median ns for the same host, seed and workload',
    }, indent=2) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
