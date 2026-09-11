"""Compact, lossless iteration records for selected Google Benchmark runs."""

import csv
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import statistics
import subprocess


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_measurements(directory):
    directory = Path(directory)
    if (directory / "benchmark.json").exists():
        return (json.loads((directory / "run.json").read_text()),
                json.loads((directory / "benchmark.json").read_text()))
    receipt = json.loads((directory / "provenance.json").read_text())
    for name in ("samples.csv", "accounting.csv"):
        if digest(directory / name) != receipt["files_sha256"][name]:
            raise ValueError(f"compact evidence hash mismatch: {name}")
    rows = []
    with (directory / "samples.csv").open(newline="") as handle:
        for record in csv.DictReader(handle):
            row = {"run_type": "iteration"}
            for key, value in record.items():
                if value == "":
                    continue
                try:
                    row[key] = json.loads(value) if key not in {"name", "time_unit"} else value
                except ValueError:
                    row[key] = value
            rows.append(row)
    return receipt, {"context": receipt["context"], "benchmarks": rows}


def compact_run(source, destination):
    """Called after bundle verification; destination is a new staging directory."""
    source, destination = Path(source), Path(destination)
    raw_receipt = json.loads((source / 'run.json').read_text())
    if 'compact' in raw_receipt:
        return compact_files(source, destination, raw_receipt)
    receipt, benchmark = read_measurements(source)
    if receipt["status"] != "complete" or not receipt["source_unchanged"]:
        raise ValueError("only successful, source-stable runs can become compact evidence")
    if any(row.get("error_occurred") for row in benchmark["benchmarks"]):
        raise ValueError("benchmark reported an error")
    rows = [r for r in benchmark["benchmarks"] if r.get("run_type") == "iteration"]
    if not rows:
        raise ValueError("no individual repetitions to retain")
    # Derived aggregate rows and duplicated framework names stay in the bundle.
    omitted = {"run_type", "run_name", "family_index", "per_family_instance_index", "repetitions"}
    fields = list(dict.fromkeys(key for row in rows for key in row if key not in omitted))
    with (destination / "samples.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow({key: value if isinstance(value, str) else json.dumps(value)
                             for key, value in row.items() if key in fields})
    for name in ("accounting.csv", "summary.csv", "summary.md"):
        (destination / name).write_text((source / name).read_text())
    provenance = {
        "format": 1,
        "status": receipt["status"],
        "source_unchanged": receipt["source_unchanged"],
        "started_utc": receipt["started_utc"],
        "source_digest": receipt["source_digest"],
        "source_archive_sha256": digest(source / "source.tar.gz"),
        "git_head": (source / "git-head.stdout").read_text().strip(),
        "config": {k: v for k, v in receipt["config"].items() if k not in {"build_dir", "output"}},
        "platform": receipt["platform"],
        "compiler": (source / "compiler.stdout").read_text().strip(),
        "configure_flags": [arg for step in receipt["commands"] if step["name"] == "configure"
                            for arg in step["argv"] if arg.startswith("-D")],
        "context": {k: v for k, v in benchmark["context"].items() if k != "executable"},
        "files_sha256": {name: digest(destination / name) for name in ("samples.csv", "accounting.csv")},
        "full_bundle": "artifact.json",
    }
    (destination / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


def compact_files(source, destination, receipt):
    if receipt['status'] != 'complete' or not receipt['source_unchanged']:
        raise ValueError('Only a successful run can install compact evidence')
    selected = receipt['compact']['files']
    if not selected or len(set(selected)) != len(selected):
        raise ValueError('Choose distinct compact files')
    for name in selected:
        p = Path(name)
        if p.is_absolute() or '..' in p.parts or name in ('artifact.json', 'provenance.json', '.gitattributes'):
            raise ValueError(f'Invalid compact file: {name}')
        expected = receipt['artifact_sha256'].get(name)
        if expected is None or digest(source / name) != expected:
            raise ValueError(f'Compact input missing or changed: {name}')
        (destination / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, destination / name)
    # Preserve exactly the selected bytes, including pre-existing CRLF CSVs.
    (destination / '.gitattributes').write_text('* -text\n')
    provenance = {k: receipt[k] for k in ('status', 'source_unchanged', 'started_utc', 'source_digest', 'config', 'platform')}
    provenance.update(format=2, files_sha256={name: digest(destination / name) for name in selected},
                      regenerate=receipt['compact']['regenerate'], full_bundle='artifact.json',
                      source_archive_sha256=digest(source / 'source.tar.gz'),
                      inputs={k: {f: v[f] for f in ('id', 'key')} for k, v in receipt.get('inputs', {}).items()})
    for file, key in (('compiler.stdout', 'compiler'), ('git-head.stdout', 'git_head')):
        if (source / file).exists():
            provenance[key] = (source / file).read_text().strip()
    provenance['configure_flags'] = [arg for step in receipt['commands'] if step['name'] == 'configure'
                                     for arg in step['argv'] if arg.startswith('-D')]
    provenance['commands'] = [{'name': step['name'], 'returncode': step['returncode'], 'required': step.get('required', True)}
                              for step in receipt['commands']]
    (destination / 'provenance.json').write_text(json.dumps(provenance, indent=2, sort_keys=True) + '\n')


def git_root(directory):
    directory = Path(directory).resolve()
    while not directory.is_dir():
        directory = directory.parent
    result = subprocess.run(['git', '-C', str(directory), 'rev-parse', '--show-toplevel'],
                            capture_output=True, text=True)
    return Path(result.stdout.strip()).resolve() if result.returncode == 0 else None


def git_revision(root, tree):
    if tree == ':':  # The index, including staged bytes rather than worktree bytes.
        return ''
    return subprocess.check_output(['git', '-C', str(root), 'rev-parse', '--verify',
                                    tree + '^{tree}'], text=True).strip()


def verify_compact(directory, *, tree=None):
    """Verify local bytes, or Git blobs (tree=':' for the index, otherwise a ref)."""
    directory = Path(directory).resolve()
    if tree is None:
        read = lambda name: (directory / name).read_bytes()
    else:
        root = git_root(directory)
        if root is None:
            raise ValueError('Git evidence verification requires a repository')
        prefix = directory.relative_to(root).as_posix()
        revision = git_revision(root, tree)

        def read(name):
            result = subprocess.run(['git', '-C', str(root), 'cat-file', 'blob',
                                     f'{revision}:{prefix}/{name}'], capture_output=True)
            if result.returncode:
                raise ValueError(f'Missing Git evidence in {tree}: {prefix}/{name}')
            return result.stdout

    def safe_read(name):
        if Path(name).is_absolute() or '..' in Path(name).parts:
            raise ValueError('Invalid compact evidence path')
        return read(name)

    meta = json.loads(read('provenance.json'))
    for name, expected in meta.get('files_sha256', meta.get('compact_sha256', {})).items():
        if hashlib.sha256(safe_read(name)).hexdigest() != expected:
            raise ValueError(f'Compact evidence changed: {name}')
    if meta.get('full_bundle'):
        json.loads(safe_read(meta['full_bundle']))
    return meta


def verify_exports(directory, *, tree=None):
    """Find compact manifests under a directory in the same tree being verified."""
    directory = Path(directory).resolve()
    if tree is None:
        manifests = sorted(directory.rglob('provenance.json'))
    else:
        root = git_root(directory)
        if root is None:
            raise ValueError('Git evidence verification requires a repository')
        revision = git_revision(root, tree)
        command = ['ls-files', '--cached', '-z'] if tree == ':' else ['ls-tree', '-rz', '--name-only', revision]
        names = subprocess.check_output(['git', '-C', str(root), *command, '--',
                                         directory.relative_to(root).as_posix()]).decode().split('\0')
        manifests = [root / name for name in names if name.endswith('/provenance.json')]
    if not manifests:
        raise ValueError(f'No compact evidence manifests in {directory}')
    for path in manifests:
        verify_compact(path.parent, tree=tree)
    return len(manifests)


def summarize(inputs, destination, *, counters=(), pattern=None, artifact=None):
    """Summarize ordinary Google Benchmark JSON; interpretation stays with the study."""
    selected = re.compile(pattern) if pattern else None
    groups, sources = {}, {}
    units = {'ns': 1, 'us': 1000, 'ms': 1_000_000, 's': 1_000_000_000}
    seen_counters = set()
    for label, path in inputs:
        path = Path(path)
        if not label or label in sources:
            raise ValueError('choose distinct input labels, such as zen5=path/to/samples.json')
        raw = path.read_bytes()
        data = json.loads(raw)
        sources[label] = {'path': str(path.resolve()), 'sha256': hashlib.sha256(raw).hexdigest(),
                          'context': data.get('context', {})}
        count = 0
        for row in data['benchmarks']:
            name = row.get('run_name', row.get('name', ''))
            if row.get('run_type', 'iteration') != 'iteration' or (selected and not selected.search(name)):
                continue
            if row.get('error_occurred'):
                raise ValueError(f'{label}/{name}: benchmark error: {row.get("error_message", "unspecified")}')
            if not name or row.get('iterations', 0) <= 0 or row.get('time_unit') not in units:
                raise ValueError(f'{label}/{name}: missing case, iterations or supported time unit')
            scale = units[row['time_unit']]
            sample = {'cpu_ns': row['cpu_time'] * scale, 'real_ns': row['real_time'] * scale}
            if any(not math.isfinite(v) or v < 0 for v in sample.values()):
                raise ValueError(f'{label}/{name}: invalid timing')
            if 'items_per_second' in row:
                rate = row['items_per_second']
                if not math.isfinite(rate) or rate <= 0:
                    raise ValueError(f'{label}/{name}: invalid item rate')
                sample['ns_per_item'] = 1e9 / rate
            sample['counters'] = {key: row[key] for key in counters if key in row}
            seen_counters.update(sample['counters'])
            groups.setdefault((label, name, row.get('threads', 1)), []).append(sample)
            count += 1
        if not count:
            raise ValueError(f'{label}: no selected iteration rows')
    if not groups:
        raise ValueError('choose at least one benchmark input')
    if set(counters) - seen_counters:
        raise ValueError('requested counters absent: ' + ', '.join(sorted(set(counters) - seen_counters)))
    rows = []
    for (label, name, threads), samples in sorted(groups.items()):
        row = {'input': label, 'case': name, 'threads': threads, 'repetitions': len(samples)}
        for metric in ('cpu_ns', 'real_ns', 'ns_per_item'):
            values = [sample.get(metric) for sample in samples]
            row[metric] = json.dumps(values, separators=(',', ':')) if any(v is not None for v in values) else ''
            row['median_' + metric] = statistics.median(values) if all(v is not None for v in values) else ''
        for key in counters:
            values = [sample['counters'].get(key) for sample in samples]
            row['counter:' + key] = ('' if all(v is None for v in values) else
                                     json.dumps(values[0] if all(v == values[0] for v in values) else values,
                                                separators=(',', ':'), allow_nan=False))
        rows.append(row)
    destination = Path(destination)
    reference = Path(artifact).read_bytes() if artifact else None
    if reference is not None:
        json.loads(reference)
    destination.mkdir(parents=True, exist_ok=False)
    with (destination / 'cases.csv').open('w', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    metadata = {'format': 1, 'kind': 'google-benchmark-summary', 'inputs': sources,
                'filter': pattern, 'counters': list(counters), 'cases': len(rows),
                'units': 'CPU/real times are ns per benchmark iteration. ns_per_item uses the reported item rate; an item means what the benchmark counts, not necessarily a value or byte.',
                'files_sha256': {'cases.csv': digest(destination / 'cases.csv')}}
    if reference is not None:
        (destination / 'artifact.json').write_bytes(reference)
        metadata['full_bundle'] = 'artifact.json'
    (destination / 'provenance.json').write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n')
    return metadata


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Produce compact tables from selected Google Benchmark JSON.')
    commands = parser.add_subparsers(dest='command', required=True)
    summary = commands.add_parser('summarize', help='one row per case, with repetitions, medians and selected counters')
    summary.add_argument('inputs', nargs='+', metavar='[LABEL=]JSON')
    summary.add_argument('--output', type=Path, required=True, help='new output directory')
    summary.add_argument('--filter', help='optional case-name regular expression')
    summary.add_argument('--counter', action='append', default=[], help='retain this counter; repeat as needed')
    summary.add_argument('--artifact', type=Path, help='attach an existing recovery reference; no upload')
    args = parser.parse_args()
    inputs = []
    for index, value in enumerate(args.inputs, 1):
        label, separator, path = value.partition('=')
        inputs.append((label, Path(path)) if separator else (f'input{index}', Path(value)))
    try:
        result = summarize(inputs, args.output, counters=args.counter, pattern=args.filter, artifact=args.artifact)
    except (ValueError, KeyError, OSError, re.error) as error:
        parser.exit(1, f'{error}\n')
    print(f'{result["cases"]} cases: {args.output / "cases.csv"}')
    print('Timings and selected counters only; interpret comparisons in the study.')


if __name__ == '__main__':
    main()
