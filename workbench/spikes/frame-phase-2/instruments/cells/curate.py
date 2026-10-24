#!/usr/bin/env python3
"""Compact the entire first native panel, retaining every arm and repetition."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import tarfile

from panel import audit, sha, summarize


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zen', type=Path, required=True)
    p.add_argument('--graviton', type=Path, required=True)
    p.add_argument('--capture-receipt', type=Path, help='Optional original capture receipt; worker manifests suffice on recovery')
    p.add_argument('--inputs', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    expected = json.loads((args.zen / 'source-manifest.json').read_text())
    capture_digest = hashlib.sha256(json.dumps(expected, sort_keys=True).encode()).hexdigest()
    if args.capture_receipt:
        capture = json.loads(args.capture_receipt.read_text())
        assert expected == capture['source_files_sha256'] and capture_digest == capture['source_digest']
    args.output.mkdir(parents=True, exist_ok=False)
    cases, samples, sources, checks = [], [], {}, {}
    for name, root, profiles in [('zen5', args.zen, ('avx2', 'avx512')), ('graviton4', args.graviton, ('neon',))]:
        found = set()
        with tarfile.open(root / 'source.tar.gz') as archive:
            for member in archive:
                key = member.name.removeprefix('./')
                if member.isfile() and key in expected:
                    assert hashlib.sha256(archive.extractfile(member).read()).hexdigest() == expected[key], key
                    found.add(key)
        assert found == set(expected)
        assert json.loads((root / 'worker-result.json').read_text())['script_returncode'] == 0
        host = json.loads((root / 'host.json').read_text())
        sources[name] = dict(source_files_compared=len(found), host=host,
            worker_result=json.loads((root / 'worker-result.json').read_text()),
            input_artifact=json.loads((root / 'input-artifact.json').read_text()),
            full_worker_artifact=json.loads((root.parent / 'artifact.json').read_text()), job=root.parent.name)
        for profile in profiles:
            checks[profile] = {mode: json.loads((root / profile / mode).read_text()) for mode in ('check.json', 'sanitize.json')}
            identity = json.loads((root / profile / 'panel/identity.json').read_text())
            for path, digest in identity['input_sha256'].items():
                assert sha(args.inputs / Path(path).name) == digest
            assert sha(root / profile / 'bin/frame_cells') == identity['binary_sha256']
            for line in (root / profile / 'binaries.sha256').read_text().splitlines():
                digest, path = line.split(maxsplit=1)
                assert sha(root / profile / 'bin' / Path(path).name) == digest
            sources[name][profile] = dict(binary_sha256=identity['binary_sha256'],
                input_sha256={Path(path).name: digest for path, digest in identity['input_sha256'].items()},
                hardware=identity['hardware'], selected_cores=identity['selected_cores'])
            for raw in sorted((root / profile / 'panel').glob('cell-*.jsonl')):
                rows = [json.loads(line) for line in raw.read_text().splitlines()]
                # Original paths refer to the worker. Admit byte-identical
                # local corpus files before evaluating the independent oracle.
                config = rows[0]
                for key in ('a', 'b'): config[key] = str(args.inputs / Path(config[key]).name)
                config, allocation, measured = audit(rows)
                summary = summarize(config, allocation, measured)
                cell = int(raw.stem.split('-')[1])
                overshoot = max((max(w['end_ns'] - r['deadline_ns'] for w in r['workers']) for r in measured
                                 if r['deadline_ns'] is not None), default=0)
                for arm in summary['arms']:
                    cases.append(dict(profile=profile, cell=cell, operation=config['operation'],
                        a=Path(config['a']).stem, b=Path(config['b']).stem, condition=config['condition'],
                        workers=len(allocation['workers']), pool=config['pool'], rows=config['rows_per_query'],
                        threshold=config['threshold'], expected_sum=config['expected_sum'], expected_count=config['expected_count'],
                        coarse=config['coarse'], arm=arm['name'], logical_bytes=arm['logical_bytes_per_worker'],
                        capacity_bytes=arm['capacity_bytes_per_worker'], ns_per_query=arm['ns_per_query'],
                        duplicate_ns=arm['duplicate_ns'], primary_over_duplicate=arm['primary_over_duplicate'],
                        min_overlap=summary['min_overlap_fraction'], max_deadline_overshoot_ns=overshoot))
                for r in measured:
                    samples.append(dict(profile=profile, cell=cell, trial=r['trial'], position=r['position'],
                        treatment=r['treatment'], arm=r['arm'], duplicate=r['duplicate'], wall_ns=r['wall_ns'],
                        overlap_ns=r['overlap_ns'], queries=r['queries'],
                        max_deadline_overshoot_ns=max((w['end_ns'] - r['deadline_ns'] for w in r['workers']), default=0)
                            if r['deadline_ns'] is not None else 0))
    for name, rows in [('cases.csv', cases), ('samples.csv', samples)]:
        with (args.output / name).open('w') as f:
            writer = csv.DictWriter(f, list(rows[0]), lineterminator='\n'); writer.writeheader(); writer.writerows(rows)
    (args.output / 'audit.json').write_text(json.dumps(dict(capture=capture_digest,
        all_source_files_compared=True, independent_answers_schedule_allocations_intervals=True,
        cells=len(cases) // 3, samples=len(samples), checks=checks, sources=sources,
        scope='All first-panel treatments and repetitions, including losses. Ranges/individual worker records/assembly/executables in linked full worker archives.'), indent=2) + '\n')
    print(json.dumps(dict(cells=len(cases) // 3, samples=len(samples), output=str(args.output))))


if __name__ == '__main__':
    main()
