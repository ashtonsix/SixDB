#!/usr/bin/env python3
"""Generate a static explorer from retained evidence; no cloud reads or benchmark runs."""

import argparse
import csv
from datetime import datetime, timezone
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import math
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / 'workbench/results'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    if not path.is_file():
        return {}
    try:
        return json.loads(path.read_text())
    except ValueError as error:
        raise ValueError(f'{path.name}: malformed JSON') from error


def number(value):
    try:
        result = float(value)
        return result if math.isfinite(result) else None
    except (TypeError, ValueError):
        return None


def values(value):
    if isinstance(value, str) and value.startswith('['):
        try:
            value = json.loads(value)
        except ValueError:
            return []
    if not isinstance(value, list):
        value = [value]
    return [v for item in value if (v := number(item)) is not None]


def metric(samples, unit, median=None, low=None, high=None):
    samples = values(samples)
    middle = number(median)
    if middle is None:
        middle = statistics.median(samples) if samples else None
    if middle is None:
        return None
    return {'unit': unit, 'median': middle,
            'min': number(low) if number(low) is not None else min(samples, default=middle),
            'max': number(high) if number(high) is not None else max(samples, default=middle),
            'samples': samples}


def measurements(tables):
    """Recognize explicit shared formats; unknown tables remain available verbatim."""
    out = []
    for table in tables:
        fields, rows = table['fields'], table['rows']
        if {'case', 'median_cpu_ns', 'cpu_ns'} <= set(fields):
            for r in rows:
                metrics = [metric(r.get('cpu_ns'), 'CPU ns / iteration', r['median_cpu_ns'])]
                if 'median_ns_per_item' in r:
                    metrics.append(metric(r.get('ns_per_item'), 'ns / item', r['median_ns_per_item']))
                out.append({'case': r['case'], 'input': r.get('input', ''), 'table': table['name'],
                            'metrics': [m for m in metrics if m], 'counters': {
                                k.removeprefix('counter:'): v for k, v in r.items() if k.startswith('counter:')}})
        elif {'case', 'repetition', 'cpu_ns'} <= set(fields):
            groups = {}
            for r in rows:
                groups.setdefault(r['case'], []).append(r)
            for name, records in groups.items():
                out.append({'case': name, 'input': '', 'table': table['name'],
                            'metrics': [metric([r['cpu_ns'] for r in records], 'CPU ns / iteration')],
                            'counters': {k: records[0][k] for k in ('logical_values', 'selected_values', 'write_records')
                                         if k in records[0]}})
        elif {'name', 'cpu_time', 'time_unit'} <= set(fields):
            groups = {}
            for r in rows:
                if r.get('run_type', 'iteration') != 'iteration':
                    continue
                scale = {'ns': 1, 'us': 1000, 'ms': 1e6, 's': 1e9}.get(r['time_unit'])
                value = number(r['cpu_time'])
                if scale is not None and value is not None:
                    groups.setdefault(r['name'], []).append(value * scale)
            out.extend({'case': name, 'input': '', 'table': table['name'],
                        'metrics': [metric(samples, 'CPU ns / iteration')], 'counters': {}}
                       for name, samples in groups.items())
    return [r for r in out if all(r['metrics'])]


def comparisons(tables, measured):
    out = []
    samples = {(r['case'], m['unit']): m for r in measured for m in r['metrics'] if not r['input']}
    for table in tables:
        fields = set(table['fields'])
        if {'case', 'median_cpu_ns', 'control', 'ratio'} <= fields:
            for r in table['rows']:
                ratio = number(r['ratio'])
                if not r['control'] or ratio is None or ratio <= 0:
                    continue
                candidate = samples.get((r['case'], 'CPU ns / iteration')) or metric(
                    [], 'CPU ns / iteration', r['median_cpu_ns'], r.get('min_cpu_ns'), r.get('max_cpu_ns'))
                control = samples.get((r['control'], 'CPU ns / iteration'))
                out.append({'case': r['case'], 'control': r['control'], 'ratio': ratio,
                            'candidate': candidate, 'baseline': control, 'table': table['name'],
                            'basis': 'Recorded control; medians of retained repetitions'})
        elif {'case', 'base_ns', 'candidate_ns', 'ratio', 'base_samples', 'candidate_samples'} <= fields:
            # This format mixes ns/value bulk rows and ns/query resident rows.
            # Keep that distinction instead of inventing a universal ns/item unit.
            for r in table['rows']:
                unit = ('ns / value' if r['case'].startswith('bulk/') else
                        'ns / query' if r['case'].startswith('resident/') else 'ns (study-defined)')
                ratio = number(r['ratio'])
                if ratio is not None and ratio > 0:
                    out.append({'case': r['case'], 'control': 'Baseline · same named case', 'ratio': ratio,
                                'candidate': metric(r['candidate_samples'], unit, r['candidate_ns']),
                                'baseline': metric(r['base_samples'], unit, r['base_ns']),
                                'table': table['name'], 'basis': r.get('observation', 'Recorded paired comparison')})
        elif {'case', 'unit', 'before_median', 'after_median', 'after_over_before'} <= fields:
            for r in table['rows']:
                ratio = number(r['after_over_before'])
                if ratio is not None and ratio > 0:
                    out.append({'case': r['case'], 'control': 'Before · same named case', 'ratio': ratio,
                                'candidate': metric([], r['unit'], r['after_median'], r.get('after_min'), r.get('after_max')),
                                'baseline': metric([], r['unit'], r['before_median'], r.get('before_min'), r.get('before_max')),
                                'table': table['name'], 'basis': 'Recorded before/after; raw repetitions are not in this table'})
    return out


def friendly(value):
    names = {'ikea2': 'Ikea2', 'ikea-composition': 'Ikea composition', 'seriespack': 'SeriesPack',
             'ikea2-campaign': 'Ikea2 replacement campaign', 'seriespack-predecessor': 'SeriesPack predecessor',
             'bec-packed-metadata': 'BEC packed metadata'}
    return names.get(value, value.replace('-', ' ').capitalize()).replace('Seriespack', 'SeriesPack')


def date_key(value):
    """Compare folder dates and recorded timestamps without inventing time precision."""
    digits = re.sub(r'[^0-9]', '', str(value))
    return digits[:14].ljust(14, '0')


def study_for(path):
    parts = Path(path).parts
    for study in ('ikea2-campaign', 'seriespack-predecessor'):
        if study in parts:
            return friendly(study)
    if parts[:2] == ('ikea2', 'bench'):
        return friendly('ikea2-campaign')
    return friendly(parts[2] if len(parts) > 2 and parts[0] == 'workbench' else parts[0])


def scientific_context(meta, facts):
    config = meta.get('config', {})
    if not isinstance(config, dict):
        config = {}
    context = meta.get('benchmark_context', meta.get('context', {}))
    if not context:
        context = {name: value.get('context', {}) for name, value in meta.get('inputs', {}).items()
                   if isinstance(value, dict) and value.get('context')}
    def clean(value):
        if isinstance(value, dict):
            # Infrastructure account/network names and arbitrary environments aren't display evidence.
            return {k: clean(v) for k, v in value.items() if k not in {
                'host_name', 'hostname', 'env', 'subnets', 'vpc_id', 'security_group_id',
                'instance_profile', 'bucket', 'source_files', 'source_files_sha256', 'commands'}}
        if isinstance(value, list):
            return [clean(v) for v in value]
        return value
    allowed = ('machine', 'architecture', 'instance_type', 'actual_capacity', 'capacity',
               'hardware', 'tune', 'profile', 'build_type', 'cpu', 'repetitions', 'min_time')
    context_keys = ('compiler', 'configure_flags', 'units', 'scope', 'limits', 'filter', 'counters',
                    'source_digest', 'source_archive_sha256', 'source', 'job', 'checks', 'cpu',
                    'profile', 'tune', 'minimum_seconds', 'repetitions', 'production_library_sha256')
    return {'configuration': {k: clean(config[k]) for k in allowed if k in config},
            'measurement_context': clean(context),
            'recorded': {k: clean(meta[k]) for k in context_keys if k in meta},
            'run_facts': {k: clean(facts[k]) for k in context_keys if k in facts}}


def load_run(directory, files):
    relative = directory.relative_to(ROOT).as_posix()
    before, _, suffix = relative.partition('/evidence/')
    study = study_for(before)
    owner = directory
    while not (owner / 'provenance.json').is_file() and owner != ROOT and 'evidence' in owner.parts:
        owner = owner.parent
    meta_path = owner / 'provenance.json'
    meta = read_json(meta_path)
    facts = read_json(directory / 'run-facts.json')
    context = scientific_context(meta, facts)
    config = context['configuration']
    machine = config.get('machine') or config.get('tune') or facts.get('tune') or meta.get('tune')
    if not machine:
        machine = next((m for m in ('zen5', 'granite-rapids', 'neoverse-v2') if m in relative), None)
    if not machine:
        machine = 'Zen 5' if '/zen' in relative else 'Granite Rapids' if '/gnr' in relative else 'Neoverse V2' if '/v2' in relative else 'Not recorded'
    machine = {'zen5': 'Zen 5', 'granite-rapids': 'Granite Rapids', 'neoverse-v2': 'Neoverse V2'}.get(machine, machine)
    raw_config = meta.get('config') or {}
    environment = raw_config.get('env') or {}
    profile = (environment.get('IKEA_PROFILE') or environment.get('IKEA2_PROFILE') or facts.get('profile') or
               meta.get('profile') or config.get('profile') or '')
    context['configuration']['profile'] = profile
    tables, exposed = [], []
    for p in sorted(files):
        if p.suffix != '.csv':
            continue
        raw = p.read_bytes()
        text = raw.decode('utf-8-sig')
        reader = csv.DictReader(io.StringIO(text, newline=''))
        rows = list(reader)
        tables.append({'name': p.name, 'fields': reader.fieldnames or [], 'rows': rows})
        exposed.append({'name': p.name, 'text': raw.decode('utf-8'), 'sha256': sha(raw),
                        'bytes': len(raw), 'rows': len(rows), 'columns': len(reader.fieldnames or [])})
    measured = measurements(tables)
    paired = comparisons(tables, measured)
    hashes = meta.get('files_sha256', meta.get('compact_sha256', {}))
    failures = []
    for name, expected in hashes.items():
        p = (owner / name).resolve()
        if not p.is_relative_to(ROOT) or not p.is_file():
            failures.append({'file': name, 'problem': 'missing retained member'})
        elif sha(p.read_bytes()) != expected:
            failures.append({'file': name, 'problem': 'hash mismatch'})
    integrity = {'status': 'mismatch' if failures else 'verified' if hashes else 'snapshot',
                 'checked_members': len(hashes), 'failures': failures,
                 'provenance_path': str(meta_path.relative_to(ROOT)) if meta_path.is_file() else None,
                 'provenance_sha256': sha(meta_path.read_bytes()) if meta_path.is_file() else None}
    notes_path = directory / 'summary.md'
    notes = notes_path.read_text() if notes_path.is_file() else ''
    notes = '\n'.join(notes.splitlines()[:next((i for i, line in enumerate(notes.splitlines())
                                               if line.startswith('|')), len(notes.splitlines()))])[:6000]
    build = {name.removesuffix('.json'): read_json(directory / name)
             for name in ('build-summary.json', 'compile-cost.json', 'code-size.json') if (directory / name).is_file()}
    date = (meta.get('benchmark_context', {}).get('date') or meta.get('started_utc') or
            next(iter(re.findall(r'20\d{6}', relative)), ''))
    label = suffix or directory.name
    return {'id': relative, 'study': study, 'label': label, 'machine': machine,
            'profile': profile, 'date': date, 'context': context, 'notes': notes,
            'files': exposed, 'measurements': measured, 'comparisons': paired, 'build': build,
            'integrity': integrity, 'artifact': read_json(directory / 'artifact.json') or read_json(owner / 'artifact.json')}


def generate(output, includes):
    output = output.resolve()
    if not output.is_relative_to(ROOT / 'build') or output == ROOT / 'build':
        raise ValueError('Generate into a directory beneath this checkout\'s build/')
    if output.exists() and not (output / 'catalog.json').is_file():
        raise ValueError('Output exists without a results catalog; choose a new directory')
    visible = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT).decode().split('\0')
    groups = {}
    for name in sorted(set(visible) - {''}):
        p = ROOT / name
        if 'evidence' not in p.parts or not p.is_file() or not (
                name.startswith('workbench/') or name.startswith('ikea2/bench/')):
            continue
        if includes and not any(p.is_relative_to(x) for x in includes):
            continue
        if p.suffix == '.csv' or p.name in {'build-summary.json', 'compile-cost.json', 'code-size.json'}:
            groups.setdefault(p.parent, []).append(p)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.results-', dir=output.parent))
    catalog = {'format': 1, 'generated': datetime.now(timezone.utc).isoformat(), 'runs': [], 'errors': []}
    try:
        for name in ('index.html', 'app.js', 'style.css'):
            shutil.copy2(APP / name, stage / name)
        (stage / 'data').mkdir()
        for directory, files in groups.items():
            try:
                run = load_run(directory, files)
                data = json.dumps(run, separators=(',', ':'), allow_nan=False).encode()
                location = 'data/' + sha(data)[:20] + '.json'
                (stage / location).write_bytes(data)
                catalog['runs'].append({k: run[k] for k in ('id', 'study', 'label', 'machine', 'profile', 'date')} | {
                    'data': location, 'comparisons': len(run['comparisons']), 'measurements': len(run['measurements']),
                    'tables': len(run['files']), 'build': bool(run['build']), 'integrity': run['integrity']['status']})
            except (OSError, ValueError, TypeError, KeyError) as error:
                catalog['errors'].append({'path': str(directory.relative_to(ROOT)), 'error': str(error)})
        catalog['runs'].sort(key=lambda r: (date_key(r['date']), r['id']), reverse=True)
        (stage / 'catalog.json').write_text(json.dumps(catalog, indent=2) + '\n')
        template = (APP / 'index.html').read_text().replace('<link rel="stylesheet" href="style.css">',
                    '<style>' + (APP / 'style.css').read_text() + '</style>')
        template = template.replace('<script src="app.js" defer></script>',
                    '<script>/*__SIXDB_SNAPSHOT__*/</script><script>' + (APP / 'app.js').read_text() + '</script>')
        (stage / 'portable-template.html').write_text(template)
        if output.exists():
            shutil.rmtree(output)
        stage.rename(output)
    except BaseException:
        shutil.rmtree(stage, ignore_errors=True)
        raise
    print(f'{len(catalog["runs"])} runs, {sum(r["comparisons"] for r in catalog["runs"])} recorded comparisons, '
          f'{sum(r["tables"] for r in catalog["runs"])} tables → {output}', flush=True)
    if catalog['errors']:
        print(f'{len(catalog["errors"])} exports need attention; see catalog.json and the UI.', flush=True)
    return catalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/results')
    parser.add_argument('--include', action='append', type=Path, default=[], help='limit to evidence beneath a directory; repeat as needed')
    parser.add_argument('--serve', action='store_true', help='serve the generated snapshot on localhost')
    parser.add_argument('--port', type=int, default=8766)
    args = parser.parse_args()
    try:
        generate(args.output, [p.resolve() for p in args.include])
    except (ValueError, OSError) as error:
        parser.error(str(error))
    if args.serve:
        handler = lambda *a, **kw: SimpleHTTPRequestHandler(*a, directory=str(args.output.resolve()), **kw)
        server = ThreadingHTTPServer(('127.0.0.1', args.port), handler)
        print(f'http://127.0.0.1:{server.server_port}', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()


if __name__ == '__main__':
    main()
