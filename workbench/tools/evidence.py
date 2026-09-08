"""Compact, lossless iteration records for selected Google Benchmark runs."""

import csv
import hashlib
import json
from pathlib import Path
import shutil


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


def verify_compact(directory):
    directory = Path(directory)
    meta = json.loads((directory / 'provenance.json').read_text())
    for name, expected in meta.get('files_sha256', meta.get('compact_sha256', {})).items():
        if Path(name).is_absolute() or '..' in Path(name).parts:
            raise ValueError('Invalid compact evidence path')
        if digest(directory / name) != expected:
            raise ValueError(f'Compact evidence changed: {name}')
    return meta
