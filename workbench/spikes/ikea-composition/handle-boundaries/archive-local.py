#!/usr/bin/env python3
"""Archive the unique local cohesion proofs without duplicating durable sources.

Selection follows the completed ABI/actual-caller/control questions. Immutable
outputs are hard-linked into staging; small maintained sources are copied.
Nothing is evicted. Every omitted snapshot byte must match a named R3/R4 source
manifest already recoverable from its worker artifact. The archive retains a
per-file recovery map; the Git context stays compact.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path('workbench/spikes/ikea-composition/handle-boundaries')
EVIDENCE = Path('workbench/spikes/ikea-composition/evidence/interface-cohesion-v2-20261023')
BUILD = Path('build/ikea-cohesion-v2')
DIRECTORIES = ('handle-boundaries', 'predicate-storage-r4', 'predicate-tight-r4',
               'predicate-tight-address-r4', 'directory-codegen-r3', 'codegen-final-r4',
               'tp-api-final-codegen', 'tp-api-final-sources', 'native-r4-filter-audit', 'mutation-ablation')
EXTRA = ('native-r4-sources/manifest.json', 'qualify-tp-api-final.py', 'audit-codegen-final.py',
         'predicate-wrapper-smoke.json', 'predicate-native-controls-manifest.json', 'predicate-native-dryrun.json')
ARTIFACTS = {
    'R3': 's3://calico-fleet-artifacts/sixdb/artifacts/sha256/aa034538ead1c0524dec9401e5237877bd8c3d5e77c79c889c8933ca8ba986fe.tar.gz',
    'R4': 's3://calico-fleet-artifacts/sixdb/artifacts/sha256/5e6792cd78b5512e570484a6474f7f5706b19ded26b70f9c682be4d4e678cdf9.tar.gz'}


def sha(path):
    value = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def snapshots():
    result = []
    for arm in ('cached24', 'generic16', 'predicate16', 'final16'):
        result.append(BUILD / 'handle-boundaries/sources' / arm)
    for control in ('predicate-storage-r4', 'predicate-tight-r4', 'predicate-tight-address-r4'):
        result.append(BUILD / control / 'source')
    for arm in ('before', 'after'):
        result.append(BUILD / 'directory-codegen-r3/sources' / arm)
    result.append(BUILD / 'tp-api-final-sources/after')
    for arm in ('final-args', 'typed-u16-control', 'inline-footprint'):
        result.append(BUILD / 'mutation-ablation' / arm)
    return result


def selection():
    manifests = {'R3': ROOT / 'build/captures/ikea-cohesion-v2-native-r3/cohesion-inputs/manifest.json',
                 'R4': ROOT / BUILD / 'native-r4-sources/manifest.json'}
    bases = {}
    for version, path in manifests.items():
        data = json.loads(path.read_text())
        assert len(ARTIFACTS[version].rsplit('/', 1)[1].removesuffix('.tar.gz')) == 64
        for arm, hashes in data['source_sha256'].items():
            bases[version + '/' + arm] = {'files': hashes,
                'manifest_sha256': data['source_manifest_sha256'][arm], 'artifact': ARTIFACTS[version],
                'recovery': 'Fetch worker artifact, extract source.tar.gz, then cohesion-inputs/' + arm + '/'}
    candidates = set()
    for name in DIRECTORIES:
        folder = ROOT / BUILD / name
        assert folder.is_dir(), folder
        candidates.update(p.relative_to(ROOT) for p in folder.rglob('*') if p.is_file())
    candidates.update(BUILD / name for name in EXTRA)
    candidates.add(manifests['R3'].relative_to(ROOT))
    candidates.update(p.relative_to(ROOT) for p in (ROOT / HERE).iterdir()
                      if p.suffix in ('.py', '.sh', '.cpp', '.h', '.md'))
    candidates.update(p.relative_to(ROOT) for p in (ROOT / EVIDENCE).glob('*.json'))
    candidates.update(p.relative_to(ROOT) for p in (ROOT / EVIDENCE).glob('*.patch'))
    candidates.update(Path('workbench/spikes/ikea-composition') / name for name in
        ('cohesion-codegen.py', 'cohesion-v2-codegen.py', 'cohesion-directory-codegen.py',
         'cohesion-v2-native.py', 'cohesion-native/CMakeLists.txt'))
    kept, omitted = {}, {}
    source_roots = snapshots()
    for relative in sorted(candidates):
        if relative.parent == EVIDENCE and relative.name in ('local-objects-artifact.json', 'local-objects-context.json'):
            continue
        if '__pycache__' in relative.parts or relative.suffix in ('.pyc', '.pyo', '.tmp'):
            continue
        path = ROOT / relative
        assert path.is_file() and not path.is_symlink(), path
        digest = sha(path)
        entry = {'sha256': digest, 'bytes': path.stat().st_size}
        source = next((relative.relative_to(p) for p in source_roots if relative.is_relative_to(p)), None)
        match = next((name for name, b in bases.items() if source is not None and
                      b['files'].get(source.as_posix()) == digest), None)
        if match:
            omitted[relative.as_posix()] = {**entry, 'base': match, 'source_path': source.as_posix()}
        else:
            kept[relative.as_posix()] = entry
    return kept, omitted, {name: {k: v for k, v in data.items() if k != 'files'} for name, data in bases.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staging', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--context', type=Path)
    parser.add_argument('--preview', action='store_true')
    args = parser.parse_args()
    kept, omitted, bases = selection()
    totals = Counter()
    for name, row in kept.items():
        root = next((p for p in DIRECTORIES if name.startswith(str(BUILD / p) + '/')), 'sources-and-context')
        totals[root + '/files'] += 1
        totals[root + '/bytes'] += row['bytes']
        totals[root + '/objects'] += name.endswith('.o')
    summary = {'selected_files': len(kept), 'selected_raw_bytes': sum(v['bytes'] for v in kept.values()),
        'omitted_source_files': len(omitted), 'omitted_source_bytes': sum(v['bytes'] for v in omitted.values()),
        'by_root': dict(totals)}
    assert totals['codegen-final-r4/objects'] == 69
    assert totals['tp-api-final-codegen/objects'] == 19
    assert totals['handle-boundaries/objects'] == 79
    assert totals['directory-codegen-r3/objects'] == 12
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    if args.preview:
        return
    assert args.reference and args.context
    staging = args.staging.resolve()
    assert not staging.exists(), 'choose fresh staging; never overwrite completed evidence'
    for name in kept:
        destination = staging / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        if name.startswith('build/'):
            os.link(ROOT / name, destination)
        else:
            shutil.copy2(ROOT / name, destination)
    manifest = {'scope': __doc__, 'files': kept, 'omitted_snapshots': omitted, 'source_recovery': bases,
        'summary': summary, 'source_script_sha256': sha(Path(__file__)),
        'exclusions': ['Existing journal artifact, full durable R3/R4 source snapshots, remote native binaries/logs, compiler caches/temp, older superseded codegen runs.'],
        'recovery': 'artifacts.py fetch REFERENCE NEW_DIRECTORY; selected original repository-relative paths are preserved. Recover omitted source paths from the named worker source.tar.gz and matching source base.'}
    save(staging / 'recovery-manifest.json', manifest)
    # Generic files publication performs deterministic packing, source stability
    # checks, conditional upload and complete compressed download verification.
    sys.path.insert(0, str(ROOT / 'workbench/tools'))
    import artifacts
    with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='.cohesion-evidence-') as temporary:
        reference = artifacts.publish(staging, Path(temporary), validate_run=False)
    save(args.reference, reference)
    context = {'scope': __doc__, 'artifact_reference': str(args.reference), 'summary': summary,
        'recovery_manifest_sha256': sha(staging / 'recovery-manifest.json'), 'source_recovery': bases,
        'artifact_tool_sha256': sha(ROOT / 'workbench/tools/artifacts.py'),
        'selection_script_sha256': sha(Path(__file__)),
        'reproduce_selection': 'python3 ' + str(HERE / 'archive-local.py') + ' --staging UNUSED --preview',
        'limits': 'This is durable recovery, not a new correctness/performance qualification. No local file was deleted. Native predicate-control measurements remain in their own worker archives.'}
    save(args.context, context)
    print('Durable reference:', args.reference, flush=True)


if __name__ == '__main__':
    main()
