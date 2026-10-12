#!/usr/bin/env python3
"""Keep all complete-operation summaries; leave raw observations in the worker archive."""
import argparse
import csv
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = json.loads((args.results / 'source.json').read_text())
    collection = args.results.parent / 'collection.json'
    receipt = json.loads(collection.read_text()) if collection.exists() else {}
    recovery = receipt.get('artifact')
    host = json.loads((args.results / 'host.json').read_text()) if (args.results / 'host.json').exists() else {}
    summaries, contexts = [], []
    for path in sorted(args.results.glob('*/samples.jsonl')):
        records = [json.loads(line) for line in path.read_text().splitlines()]
        context = records[0]
        if context.get('type') != 'context' or context['source_id'] != source['sha256']:
            raise ValueError(f'{path}: missing or mismatched source identity')
        binary = path.parent / 'streampack_implementation_orbital_bench'
        if binary.exists() and hashlib.sha256(binary.read_bytes()).hexdigest() != context['binary_sha256']:
            raise ValueError(f'{path}: measured binary mismatch')
        archived = receipt.get('members', {}).get(binary.relative_to(args.results).as_posix())
        if archived and archived['sha256'] != context['binary_sha256']:
            raise ValueError(f'{path}: archived measured binary mismatch')
        checks = path.parent / 'checks.txt'
        contexts.append(dict(profile=path.parent.name, context=context,
            raw_member=path.relative_to(args.results).as_posix(),
            raw_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            binary_hash_verified=bool(binary.exists() or archived),
            checks=checks.read_text(), checks_sha256=hashlib.sha256(checks.read_bytes()).hexdigest()))
        for line, row in enumerate(records, 1):
            if row['type'] != 'summary':
                continue
            if row.get('oracle') != 'passed':
                raise ValueError(f'{path}:{line}: unsuccessful logical oracle')
            flat = dict(profile=path.parent.name, raw_line=line,
                **{k: v for k, v in row.items() if k not in ('type', 'pool')})
            flat.update({f'pool_{k}': v for k, v in row['pool'].items()})
            summaries.append(flat)
    if not summaries:
        raise ValueError('no completed operation summaries')
    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / 'summaries.csv').open('w', newline='') as target:
        writer = csv.DictWriter(target, fieldnames=list(summaries[0]))
        writer.writeheader(); writer.writerows(summaries)
    (args.output / 'context.json').write_text(json.dumps(dict(
        selection='Every successful complete-operation summary and repetition; raw individual observations are archived.',
        inputs=contexts, source=source, raw_artifact=recovery,
        hardware={k: host[k] for k in ('cpu', 'kernel', 'compiler', 'allowed_cpus', 'instance', 'worker') if k in host},
        report_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()), indent=2) + '\n')
    print(f'{len(summaries)} summaries from {len(contexts)} profiles')


if __name__ == '__main__':
    main()
