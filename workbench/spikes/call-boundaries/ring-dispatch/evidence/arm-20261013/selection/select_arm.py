#!/usr/bin/env python3
"""Prepare compact ARM-gap evidence. Never run a benchmark or archived code.

Default is an in-memory preview. --output selects into a NEW ignored directory.
The caller settles the candidate and edits the authored configuration first.
"""
import argparse
import csv
import difflib
import gzip
import hashlib
import io
import json
import math
from pathlib import Path
import re
import tarfile

ARMS = ('abseil', 'abseil_prefetch', 'fused', 'manual', 'manual_hints', 'ring', 'ring_hints')
SOURCE_FILES = ('workbench/spikes/hashtable/table_ring.cpp',
                'workbench/spikes/hashtable/table.h', 'loom/include/loom/ring.h',
                'workbench/spikes/hashtable/CMakeLists.txt', 'run-arm-serial.sh',
                'controls/provenance.json')


def check(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load(path):
    return json.loads(path.read_text())


def relative(name):
    p = Path(name)
    check(not p.is_absolute() and '..' not in p.parts and p.parts,
          'unsafe evidence path: ' + name)
    return p


class Selection:
    def __init__(self):
        self.data = {}
        self.payloads = []
        self.by_hash = {}

    def add(self, name, data, origin=None, deduplicate=False):
        relative(name)
        key = (digest(data), len(data))
        if deduplicate and key in self.by_hash:
            existing = self.by_hash[key]
            if origin is not None and origin not in existing['origins']:
                existing['origins'].append(origin)
            return existing['path']
        check(name not in self.data, 'duplicate selected path: ' + name)
        item = {'path': name, 'bytes': len(data), 'sha256': key[0],
                'origins': [] if origin is None else [origin]}
        self.data[name] = data
        self.payloads.append(item)
        self.by_hash.setdefault(key, item)
        return name

    def json(self, name, value, origin=None):
        return self.add(name, (json.dumps(value, indent=2) + '\n').encode(), origin)


def worker_member(selection, launch, archive, collection, name, deduplicate=False):
    relative(name)
    path = launch / 'results' / name
    check(path.is_file(), 'missing collected member: ' + str(path))
    data = path.read_bytes()
    recorded = collection['members'].get(name)
    check(recorded is not None and recorded['bytes'] == len(data)
          and recorded['sha256'] == digest(data), 'collected member mismatch: ' + name)
    origin = {'kind': 'worker_archive', 'job': launch.name,
              'archive_sha256': archive['sha256'], 'member': name}
    destination = 'workers/' + launch.name + '/' + name
    return selection.add(destination, data, origin, deduplicate)


def cells_from_analysis(analysis):
    result = {}
    for cell in analysis['cells']:
        for factor, value in cell['factors'].items():
            key = (cell['case'], int(cell['pass']), factor)
            check(key not in result, 'duplicate independent analysis cell')
            result[key] = value
    return result


def select_job(selection, root, spec):
    launch = root / 'build/workers' / spec['job']
    analysis_path = root / spec['analysis']
    check(analysis_path.is_file(), 'independent qualification not ready: ' + str(analysis_path))
    analysis = load(analysis_path)
    check(analysis['source']['job'] == spec['job'], 'analysis job mismatch')
    archive, collection = load(launch / 'artifact.json'), load(launch / 'collection.json')
    check(collection['artifact'] == archive, 'collection/archive reference mismatch')
    check(load(launch / 'results/worker-result.json')['state'] == 'complete', 'incomplete worker')
    manifest = load(launch / 'source-manifest.json')
    job = load(launch / 'job.json')
    check(analysis['source']['source_commit'] == job['source_commit'], 'source commit mismatch')
    check(analysis['source']['source_archive_sha256'] == job['source']['sha256'],
          'source archive qualification mismatch')
    check(analysis['source']['source_manifest_sha256'] == digest((launch / 'source-manifest.json').read_bytes()),
          'source manifest qualification mismatch')
    receipt = {'label': spec['label'], 'job': spec['job'], 'worker_archive': archive,
               'source': analysis['source'], 'capture': analysis['capture'],
               'independent_analysis_input_sha256': digest(analysis_path.read_bytes()),
               'qualification_scope': 'Captured independently validated source/binary/compile/native receipts. '
                   'The offline selected-data report verifies their bytes but does not repeat full-archive/native checks.',
               'source_archive_member': 'source.tar.gz',
               'source_archive_sha256': job['source']['sha256'],
               'source_manifest_member': 'source-manifest.json',
               'omitted_members': {k: v for k, v in collection['members'].items()
                   if k.startswith('assembly-') or k.startswith('table-ring-')
                   or k.startswith('compile-') or k.startswith('table-plans-')
                   or k.startswith('cmake-cache-')}}
    qualification = selection.json('qualification/' + spec['label'] + '.json', receipt,
        {'kind': 'derived_by_checkout_selector', 'input': spec['analysis'],
         'input_sha256': receipt['independent_analysis_input_sha256']})
    retained = {}
    common = ['executable-order.csv', 'control-provenance.json', 'worker-result.json', 'job.json', 'host.json',
              'source-before.json', 'source-after.json', 'compiler.txt', 'compiler-sha256.txt',
              'binary-sha256-' + spec['isa'] + '.txt', 'library-sha256-' + spec['isa'] + '.txt',
              'plans-sha256-' + spec['isa'] + '.txt', 'loom-validation-' + spec['isa'] + '.txt',
              'hardware-' + spec['isa'] + '.json', 'hardware-' + spec['isa'] + '.context',
              'kernel.txt', 'progress.jsonl']
    for name in common:
        retained[name] = worker_member(selection, launch, archive, collection, name, True)
    parsed_analysis = cells_from_analysis(analysis)
    order = list(csv.DictReader((launch / 'results/executable-order.csv').open()))
    cells, rows_total = [], 0
    for row in order:
        check(row['isa'] == spec['isa'], 'unexpected ISA in order receipt')
        key = (row['case'], int(row['pass']), row['factor'])
        qualified = parsed_analysis.pop(key)
        stem = spec['isa'] + '-' + row['case'] + '-' + row['factor'] + '-pass' + row['pass']
        paths = {suffix: worker_member(selection, launch, archive, collection,
                                      stem + suffix, suffix == '.context')
                 for suffix in ('.csv', '.context', '.policy.json')}
        csv_data = selection.data[paths['.csv']]
        records = list(csv.DictReader(io.StringIO(csv_data.decode())))
        reps = int(row['reps'])
        check(len(records) == 7 * reps and qualified['rows'] == len(records), 'row count')
        check([(int(r['repetition']), r['execution']) for r in records]
              == [(rep, ARMS[(i + rep) % 7]) for rep in range(reps) for i in range(7)], 'internal arm order')
        check(digest(csv_data) == qualified['csv_sha256']
              and digest(selection.data[paths['.context']]) == qualified['context_sha256'], 'analysis/raw identity')
        for r in records:
            check((int(r['entries']), int(r['concurrency']), int(r['turnovers']), int(r['seed']))
                  == (int(row['entries']), int(row['concurrency']), int(row['turnovers']), int(row['seed'])),
                  'actual execution parameters mismatch')
            check(r['queries'] == ('mixed' if row['mixed'] == '1' else 'hit'), 'mixed/hit mismatch')
            check(math.isfinite(float(r['ns_per_lookup'])) and float(r['ns_per_lookup']) > 0, 'bad timing')
        policy = json.loads(selection.data[paths['.policy.json']])
        check(policy['csv_sha256'] == digest(csv_data)
              and policy['context_sha256'] == digest(selection.data[paths['.context']]), 'policy input hashes')
        cells.append({'case': row['case'], 'pass': int(row['pass']), 'order': int(row['order']),
                      'factor': row['factor'], 'target': row['target'], 'isa': spec['isa'],
                      'parameters': {'a': int(row['a']), 'entries': int(row['entries']),
                                     'turnovers': int(row['turnovers']), 'mixed': int(row['mixed']),
                                     'concurrency': int(row['concurrency']), 'reps': reps,
                                     'seed': int(row['seed'])},
                      'csv': paths['.csv'], 'context': paths['.context'], 'policy': paths['.policy.json']})
        rows_total += len(records)
    check(not parsed_analysis and rows_total == analysis['timing_rows'], 'missing analysis/order cells')
    forced = []
    provenance = load(launch / 'results/control-provenance.json')
    for factor in provenance['factors']:
        stem = 'check-' + spec['isa'] + '-' + factor['name']
        forced.append({'factor': factor['name'], **{suffix[1:]: worker_member(selection, launch, archive,
            collection, stem + suffix, suffix == '.context') for suffix in ('.csv', '.context')}})
    for name in ('nop-prefetch-' + spec['isa'] + '.json',):
        if name in collection['members']:
            raw = (launch / 'results' / name).read_bytes()
            recorded = collection['members'][name]
            check(recorded['bytes'] == len(raw) and recorded['sha256'] == digest(raw), 'NOP receipt identity')
            full = json.loads(raw)
            compact = {key: value for key, value in full.items() if key != 'patches'}
            compact['full_receipt_member'] = name
            compact['full_receipt_sha256'] = digest(raw)
            compact['selection_limit'] = ('Full patch list recoverable from exact worker member; '
                'offline selected-data report does not revalidate every executable patch.')
            retained[name] = selection.json('qualification/' + spec['label'] + '-nop.json', compact,
                {'kind': 'derived_worker_receipt_summary', 'job': spec['job'],
                 'archive_sha256': archive['sha256'], 'member': name, 'member_sha256': digest(raw)})
    blobs = {}
    check(digest((launch / 'source.tar.gz').read_bytes()) == job['source']['sha256'], 'controller source archive hash')
    with tarfile.open(launch / 'source.tar.gz') as source:
        for name in SOURCE_FILES:
            data = source.extractfile(name).read()
            check(digest(data) == manifest[name], 'source patch input hash')
            blobs[name] = data.decode()
    return {'label': spec['label'], 'job': spec['job'], 'isa': spec['isa'], 'role': spec.get('role', 'diagnostic'),
            'qualification': qualification, 'retained_receipts': retained,
            'cells': cells, 'forced': forced, 'timing_rows': rows_total}, blobs


def select_owner_reference(selection, root, spec):
    launch = root / 'build/workers' / spec['job']
    archive, collection = load(launch / 'artifact.json'), load(launch / 'collection.json')
    runtime = load(launch / 'results/job.json')
    check(runtime['source']['digest'].startswith(spec['source_prefix']), 'owner reference source mismatch')
    check(load(launch / 'results/worker-result.json')['state'] == 'complete', 'owner reference incomplete')
    receipts = {}
    for name in ('large.csv', 'job.json', 'worker-result.json', 'codegen.json'):
        receipts[name] = worker_member(selection, launch, archive, collection, name)
    path = root / spec['qualification']
    check(digest(path.read_bytes()) == spec['qualification_sha256'], 'owner native receipt changed')
    qualification = selection.add('qualification/current-ikea-owner.json', path.read_bytes(),
        {'kind': 'owning_local_receipt', 'path': spec['qualification'], 'sha256': spec['qualification_sha256']})
    return {'label': spec['label'], 'job': spec['job'], 'source': runtime['source'],
            'source_commit': runtime['source_commit'], 'worker_archive': archive,
            'repetitions': int(runtime['config']['env']['HASHTABLE_REPEATS']),
            'csv': receipts['large.csv'], 'receipts': receipts, 'qualification': qualification,
            'scope': 'Separate source-aware current-owner reference. Tagged CSV has different input law and no checksum; '
                     'raw median/accounting verification plus captured native receipt, not full-archive revalidation.'}


def excerpts(selection, root, config, jobs):
    results = []
    for spec in config:
        job = next(j for j in jobs if j['job'] == spec['job'])
        launch = root / 'build/workers' / spec['job']
        collection = load(launch / 'collection.json')
        path = launch / 'results' / spec['member']
        check(path.is_file(), 'missing native excerpt input')
        record = collection['members'][spec['member']]
        check(path.stat().st_size == record['bytes'] and digest(path.read_bytes()) == record['sha256'],
              'native excerpt archive identity')
        ranges = [(int(a, 16), int(b, 16)) for a, b in spec['ranges']]
        check(all(a <= b and (b - a) // 4 < 64 for a, b in ranges), 'unbounded excerpt range')
        selected = [[] for _ in ranges]
        with gzip.open(path, 'rt') as stream:
            for line_number, line in enumerate(stream, 1):
                match = re.match(r'^\s*([0-9a-f]+):\s+(.+)', line)
                if not match:
                    continue
                address = int(match[1], 16)
                for i, (first, last) in enumerate(ranges):
                    if first <= address <= last:
                        selected[i].append({'source_line': line_number, 'address': match[1],
                                            'instruction': match[2].rstrip()})
        check(all(rows and int(rows[0]['address'], 16) == bounds[0]
                  and int(rows[-1]['address'], 16) == bounds[1]
                  for rows, bounds in zip(selected, ranges)), 'excerpt boundaries changed')
        value = {'job': spec['job'], 'role': spec['role'], 'source_member': spec['member'],
                 'source_member_sha256': record['sha256'], 'ranges': spec['ranges'],
                 'instructions': selected, 'scope': 'Selected native instruction bytes represented by disassembly; '
                     'full assembly and executable recover from worker archive. No throughput conclusion from instruction count alone.'}
        destination = 'native/' + spec['role'] + '.json'
        results.append(selection.json(destination, value, {'kind': 'derived_excerpt', 'job': spec['job'],
                         'member': spec['member'], 'member_sha256': record['sha256']}))
    return results


def prepare(root, config_path, output):
    config = load(config_path)
    check(config['format'] == 1, 'unsupported selector configuration')
    if output is not None:
        check(output.is_relative_to(root / 'build'), 'prepared selection must stay in ignored build space')
    selection, jobs, previous, pending = Selection(), [], None, []
    for spec in config['jobs']:
        analysis = root / spec['analysis']
        if not analysis.is_file():
            check(output is None, 'cannot seal selection with pending independent qualification: ' + spec['label'])
            pending.append(spec)
            continue
        result, blobs = select_job(selection, root, spec)
        jobs.append(result)
        if previous is not None:
            chunks = []
            for name in SOURCE_FILES:
                chunks.extend(difflib.unified_diff(previous[name].splitlines(True), blobs[name].splitlines(True),
                    fromfile='first-qualified/' + name, tofile=spec['label'] + '/' + name))
            result['source_patch'] = selection.add('source-patches/' + spec['label'] + '.patch',
                ''.join(chunks).encode(), {'kind': 'derived_source_difference',
                    'reference_job': jobs[0]['job'], 'target_job': spec['job'],
                    'source_members': list(SOURCE_FILES)})
        else:
            previous = blobs
    native = excerpts(selection, root, config.get('native_excerpts', []), jobs)
    owner_references = [select_owner_reference(selection, root, spec)
                        for spec in config.get('owner_references', [])]
    extras, extra_paths = [], {}
    for spec in config.get('local_qualification_receipts', []):
        path = root / spec['path']
        check(path.is_file(), 'missing owning qualification receipt: ' + str(path))
        check(digest(path.read_bytes()) == spec['sha256'], 'owning receipt changed; refresh selection intentionally')
        selected_path = selection.add('qualification/local/' + spec['name'], path.read_bytes(),
            {'kind': 'owning_local_receipt', 'path': spec['path'], 'sha256': spec['sha256']}, True)
        extras.append(selected_path)
        extra_paths[spec['name']] = selected_path
    selection.add('selection/select_arm.py', Path(__file__).read_bytes(),
                  {'kind': 'historical_authored_selector', 'execution': 'not imported or executed by reports'})
    selection.add('selection/plan.json', config_path.read_bytes(),
                  {'kind': 'authored_selection_configuration'})
    limits = config['limits']
    index = {'format': 1, 'status': 'preview_pending_jobs' if pending else 'selection_prepared_not_promoted',
             'selector_sha256': digest(Path(__file__).read_bytes()), 'configuration_sha256': digest(config_path.read_bytes()),
             'jobs': jobs, 'pending': pending, 'native_excerpts': native, 'local_receipts': extras,
             'owner_references': owner_references,
             'selected_proof': {key: extra_paths[name] for key, name in config.get('selected_proof', {}).items()},
             'payloads': selection.payloads, 'counts': {'payload_files': len(selection.payloads),
                 'payload_bytes': sum(x['bytes'] for x in selection.payloads),
                 'timing_rows': sum(j['timing_rows'] for j in jobs)}, 'limits': limits}
    if output is not None:
        check(not output.exists(), 'selection destination must be new: ' + str(output))
        output.mkdir(parents=True)
        for name, data in selection.data.items():
            target = output / relative(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (output / 'recovery.json').write_text(json.dumps(index, indent=2) + '\n')
        files = [x['path'] for x in selection.payloads] + ['recovery.json']
        recipe = {'format': 1, 'selection_reason': config['selection_reason'], 'files': files,
                  'regenerate': [['python3', 'workbench/spikes/call-boundaries/ring-dispatch/arm-report.py',
                                  '--evidence', '{evidence}']], 'regenerate_from': 'retained',
                  'note': 'Checkout-owned report verifies all indexed selected inputs; captured source/native qualification remains separately scoped.'}
        (output / 'selection-recipe.json').write_text(json.dumps(recipe, indent=2) + '\n')
    print(json.dumps({'counts': index['counts'], 'jobs': [j['label'] for j in jobs],
                      'pending': [j['label'] for j in pending], 'output': None if output is None else str(output)}, indent=2))
    return index


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('/home/ashtonsix/sixdb'))
    parser.add_argument('--config', type=Path, default=Path(__file__).with_name('arm-selection-plan.json'))
    parser.add_argument('--output', type=Path, help='Select bytes into a NEW ignored directory. Default preview only.')
    args = parser.parse_args()
    prepare(args.root.resolve(), args.config.resolve(), None if args.output is None else args.output.resolve())
