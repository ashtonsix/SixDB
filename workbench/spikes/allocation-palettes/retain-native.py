#!/usr/bin/env python3
"""Select native palette evidence; leave original runs and geometry evidence alone."""
import argparse
import csv
import hashlib
import json
import statistics
import sys
from pathlib import Path


def named(value):
    name, separator, path = value.partition('=')
    if not separator or not name or not path:
        raise argparse.ArgumentTypeError('expected origin=path')
    return name, Path(path).resolve()


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def records(path):
    with path.open() as source:
        return list(csv.DictReader(source))


def write(path, rows):
    if not rows:
        return
    with path.open('w') as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def files(directory):
    result = {}
    for name in ('context.json', 'summary.csv', 'samples.csv'):
        path = directory / name
        result[name] = dict(path=str(path), sha256=sha(path), bytes=path.stat().st_size)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mechanism', type=named, action='append', default=[], metavar='ORIGIN=DIRECTORY')
    parser.add_argument('--public', type=named, action='append', default=[], metavar='ORIGIN=DIRECTORY')
    parser.add_argument('--qualification', type=named, action='append', default=[], metavar='ORIGIN=DIRECTORY')
    parser.add_argument('--recovery', type=named, action='append', default=[], metavar='ORIGIN=REFERENCE_JSON')
    parser.add_argument('--output', type=Path, required=True, help='new ignored selection directory')
    args = parser.parse_args()
    if not args.mechanism and not args.public and not args.qualification:
        parser.error('provide at least one native matrix or public run')
    for values in (args.mechanism, args.public, args.qualification, args.recovery):
        if len({name for name, _ in values}) != len(values):
            parser.error('an origin can occur only once within each input kind')

    origins = {}
    comparisons, public_rows, processes = [], [], []
    for name, directory in args.mechanism:
        context = json.loads((directory / 'context.json').read_text())
        native = int(context['host_page'])
        if int(context['page']) != native:
            raise ValueError(f'{name}: policy granules are not native page evidence')
        build = context.get('build_context')
        identity = dict(native_page=native, host=context['host'], binary_sha256=context['binary_sha256'],
                        runner_recorded_source_sha256=context.get('sources', {}), build_receipt=build,
                        cases=context['cases'], options=context['options'], limits=context.get('limits', []))
        if build and build['binary_sha256'] != context['binary_sha256']:
            raise ValueError(f'{name}: recorded build and measured binary differ')
        binary = directory / 'allocation_palettes_timing'
        identity['binary_present_and_hash_verified'] = binary.exists()
        if binary.exists() and sha(binary) != context['binary_sha256']:
            raise ValueError(f'{name}: recovered binary differs from measured identity')
        origins.setdefault(name, {})['mechanism'] = dict(inputs=files(directory), identity=identity)
        for index, row in enumerate(records(directory / 'summary.csv'), 2):
            if int(row['host_page']) != native or int(row['policy_page']) != native:
                raise ValueError(f'{name}: non-native summary row {index}')
            selected = row['stage'] == 'churn' or row['workload'] == 'mixed' and (
                row['stage'].startswith('direct-') or row['stage'] in ('growth-exact', 'growth-geometric'))
            if selected:
                comparisons.append(dict(origin=name, input_summary_row=index, **row))

    for name, directory in args.public:
        context = json.loads((directory / 'context.json').read_text())
        identity = {key: context[key] for key in ('native_page', 'cpu', 'system', 'compiler', 'march', 'tune',
                    'trials', 'rounds', 'operations', 'slots', 'warmup', 'workloads', 'touch_widths',
                    'driver_sha256', 'changed_inputs', 'limits')}
        identity['sources'] = {}
        identity['binaries'] = {}
        for label, source in context['sources'].items():
            recorded = {key: value for key, value in source.items() if key != 'input_sha256'}
            recorded['input_inventory_sha256'] = hashlib.sha256(
                json.dumps(source['input_sha256'], sort_keys=True).encode()).hexdigest()
            identity['sources'][label] = recorded
        for label, measured in context['binaries'].items():
            binary = directory / (label + '-allocator')
            present = binary.exists()
            if present and sha(binary) != measured['sha256']:
                raise ValueError(f'{name}: {label} binary differs from measured identity')
            identity['binaries'][label] = dict(sha256=measured['sha256'],
                binary_present_and_hash_verified=present, configure=measured['configure'], build=measured['build'])
        origins.setdefault(name, {})['public'] = dict(inputs=files(directory), identity=identity)
        rows = records(directory / 'summary.csv')
        keys = {(r['workload'], r['touch_bytes'], r['policy']) for r in rows}
        expected = {(workload, '0' if width == 'full' else width, policy)
                    for workload in context['workloads'] for width in context['touch_widths']
                    for policy in ('baseline', 'candidate')}
        if keys != expected or len(keys) != len(rows):
            raise ValueError(f'{name}: public summary is incomplete or duplicated')
        public_rows.extend(dict(origin=name, input_summary_row=index,
                                touch_mode='full payload' if row['touch_bytes'] == '0' else '64 bytes', **row)
                           for index, row in enumerate(rows, 2))
        samples = records(directory / 'samples.csv')
        keys = sorted({(r['workload'], r['touch_bytes'], r['policy'], int(r['trial'])) for r in samples})
        for workload, width, policy, trial in keys:
            blocks = [r for r in samples if (r['workload'], r['touch_bytes'], r['policy'], int(r['trial'])) ==
                      (workload, width, policy, trial)]
            times = [float(r['block_mean_ns_per_op']) for r in blocks]
            assert len(blocks) == context['rounds'], f'{name}: incomplete process {keys}'
            assert {r['binary_sha256'] for r in blocks} == {context['binaries'][policy]['sha256']}
            processes.append(dict(origin=name, workload=workload, touch_bytes=width, policy=policy,
                trial=trial, blocks=len(blocks), median_block_ns=statistics.median(times),
                min_block_ns=min(times), max_block_ns=max(times),
                mapped_bytes=statistics.median(int(r['mapped_bytes']) for r in blocks),
                max_new_allocations=max(int(r['new_allocations']) for r in blocks),
                max_vm_calls=max(sum(int(r[k]) for k in ('mmap_calls', 'munmap_calls', 'mprotect_calls'))
                                 for r in blocks)))

    for name, directory in args.qualification:
        receipt = directory / 'validation-context.json'
        context = json.loads(receipt.read_text())
        if not context.get('passed'):
            raise ValueError(f'{name}: validation did not finish successfully')
        inventory = context['source_sha256']
        identity = {k: context[k] for k in ('native_page', 'kernel', 'machine', 'system', 'cpu',
                    'compiler', 'sdk', 'binary_sha256', 'results', 'limits') if k in context}
        identity['input_inventory_sha256'] = hashlib.sha256(json.dumps(inventory, sort_keys=True).encode()).hexdigest()
        identity['orbital_source_sha256'] = {p: digest for p, digest in inventory.items() if p.startswith('orbital/')}
        if 'validation_log_sha256' in context:
            log = directory / 'validation.log'
            if sha(log) != context['validation_log_sha256']:
                raise ValueError(f'{name}: validation log changed')
            identity['validation_log_sha256'] = sha(log)
        identity['recorded_passed'] = True
        origins.setdefault(name, {})['qualification'] = dict(receipt_sha256=sha(receipt), identity=identity)

    for name, reference in args.recovery:
        if name not in origins:
            parser.error(f'recovery origin has no measured input: {name}')
        origins[name]['recovery'] = dict(path=str(reference), sha256=sha(reference),
                                       reference=json.loads(reference.read_text()))

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    write(output / 'comparisons.csv', comparisons)
    write(output / 'public.csv', public_rows)
    write(output / 'processes.csv', processes)
    context = dict(kind=__doc__, selector_sha256=sha(Path(__file__)), command=sys.argv,
        origins=origins, selected=dict(mechanism_rows=len(comparisons), public_rows=len(public_rows),
                                      process_rows=len(processes)),
        limits=['All medians remain within their original host and run; platforms are not pooled',
                'Mechanism batch percentiles and public block means are not point-operation latency percentiles',
                'Growth has the same trace under each workload label; only mixed-labelled rows are retained',
                'Geometric class spacing and geometric reservation growth are separate axes; the latter doubles reserve while the logical extent grows in 16-byte steps',
                'Public touch_bytes=0 means full requested payload; 64 means one cache line',
                'Source and build identities are recorded from input receipts, not reconstructed from current files',
                'A missing recovered binary is not claimed verified; raw file hashes identify the original evidence',
                'Public source input inventories include unbuilt owner tests; driver checksums and shutdown checks are separate'])
    (output / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
    print(f'{len(comparisons)} mechanism medians, {len(public_rows)} public medians: {output}')


if __name__ == '__main__':
    main()
