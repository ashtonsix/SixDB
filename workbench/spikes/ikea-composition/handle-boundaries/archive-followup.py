#!/usr/bin/env python3
"""Select unique local handle follow-up proofs for artifacts.py put.

This stages immutable local output and small producing helpers, never builds,
measures, publishes or evicts. Native binaries and the final-v5 source trees
are already durable. Their exact recovery paths are recorded by reference.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[4]
BUILD = Path('build/ikea-cohesion-v2')
HERE = Path('workbench/spikes/ikea-composition/handle-boundaries')
EVIDENCE = Path('workbench/spikes/ikea-composition/evidence/interface-cohesion-v2-20261023')
DIRECTORIES = ('predicate-projection-final-v5-codegen', 'predicate-projection-final-v5-access',
               'projection-filter-linked', 'projection-blocks-linked-arm', 'journal-local-cursor',
               'access-contract-proof', 'access-owner-proof')
HELPERS = ('audit-projection-linked-filter.py', 'qualify-projection-access.py',
           'check-access-contract.py', 'check-access-codegen.py', 'check-access-owner.py',
           'check-access-owner-native.py', 'retain-access-contract.py', 'compact-projection-final.py')
JOBS = {'neon': ('20261023T045507Z-9380ce67',
                '25e42e1efb8a223dd1d1c47cfb2fc493936c8f3e4f285c734198dc4797b85524'),
        'zen': ('20261023T045513Z-1f952ffa',
                'e332d186e209dbfb3a6c8da82c1ca831df5802bf1ba7c068493407ac8cfeae7b')}
SOURCE_ROOTS = {BUILD / 'access-contract-proof/source': '',
                BUILD / 'access-contract-proof/old': 'ikea/include/',
                BUILD / 'access-owner-proof/source': ''}


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def source_recovery():
    references, archive_hash, archive_path = {}, None, None
    for profile, (job, expected) in JOBS.items():
        folder = ROOT / 'build/workers' / job
        reference = json.loads((folder / 'artifact.json').read_text())
        collection = json.loads((folder / 'collection.json').read_text())
        job_record = json.loads((folder / 'job.json').read_text())
        source = folder / 'source.tar.gz'
        actual = sha(source)
        assert reference['sha256'] == expected and collection['artifact'] == reference
        assert actual == job_record['source']['sha256'] == collection['members']['source.tar.gz']['sha256']
        assert source.stat().st_size == collection['members']['source.tar.gz']['bytes']
        assert archive_hash is None or actual == archive_hash
        archive_hash, archive_path = actual, source
        references[profile] = {'job': job, 'artifact': reference,
            'source_archive_member': 'source.tar.gz', 'source_archive_sha256': actual,
            'source_archive_bytes': source.stat().st_size,
            'job_record_sha256': sha(folder / 'job.json'),
            'collection_record_sha256': sha(folder / 'collection.json'),
            'recovery': 'artifacts.py fetch ARTIFACT NEW --file source.tar.gz; extract source.tar.gz; '
                        'projection-inputs/{before,after}/ contains the qualified source arms.'}
    # Verify every source member, not merely the outer archive hash or an index.
    folder = archive_path.parent
    expected = json.loads((folder / 'source-manifest.json').read_text())
    seen, manifest = {}, None
    with tarfile.open(archive_path, mode='r|gz') as archive:
        for member in archive:
            assert member.isfile() and member.name not in seen
            value = archive.extractfile(member).read()
            seen[member.name] = hashlib.sha256(value).hexdigest()
            if member.name == 'projection-inputs/manifest.json':
                manifest = json.loads(value)
    assert seen == expected and manifest['variant'] == 'copied-projection-final'
    assert manifest['parent_manifest_sha256'] == '3b4f6992bc7f256675534d70c5140eb8e2b540ac1b1ae1dfea45542daf24e937'
    bases = {}
    for arm, hashes in manifest['source_sha256'].items():
        assert hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest() == manifest['source_manifest_sha256'][arm]
        assert all(seen['projection-inputs/' + arm + '/' + name] == digest for name, digest in hashes.items())
        bases[arm] = {'files': hashes, 'source_manifest_sha256': manifest['source_manifest_sha256'][arm],
                      'source_prefix': 'projection-inputs/' + arm + '/'}
    return bases, {'native_references': references, 'verified_source_archive_members': len(seen),
        'source_archive_sha256': archive_hash, 'source_manifest_file_sha256': seen['projection-inputs/manifest.json'],
        'source_maps': {arm: {k: v for k, v in base.items() if k != 'files'} for arm, base in bases.items()},
        'verification_scope': 'Existing collected source archive matches job and collection hashes; all members '
                              'match the worker source manifest. No native archive or ELF is duplicated.'}


def select(bases):
    candidates = set()
    for name in DIRECTORIES:
        folder = ROOT / BUILD / name
        assert folder.is_dir()
        candidates.update(p.relative_to(ROOT) for p in folder.rglob('*') if p.is_file())
    candidates.update(BUILD / name for name in HELPERS)
    candidates.add(BUILD / 'predicate-projection-final-v5-sources/manifest.json')
    candidates.update(p.relative_to(ROOT) for p in (ROOT / HERE).iterdir()
                      if p.suffix in ('.py', '.sh', '.cpp', '.h'))
    candidates.update(EVIDENCE / name for name in ('local-objects-artifact.json', 'local-objects-context.json'))
    kept, omitted = {}, {}
    for relative in sorted(candidates):
        if '__pycache__' in relative.parts or relative.suffix in ('.pyc', '.pyo', '.tmp'):
            continue
        assert relative.name not in ('CMakeCache.txt', 'build.ninja', '.ninja_deps', '.ninja_log')
        path = ROOT / relative
        assert path.is_file() and not path.is_symlink()
        digest = sha(path)
        row = {'sha256': digest, 'bytes': path.stat().st_size}
        canonical = None
        for source_root, prefix in SOURCE_ROOTS.items():
            if relative.is_relative_to(source_root):
                canonical = prefix + relative.relative_to(source_root).as_posix()
                if canonical == 'access.cpp':
                    canonical = 'ikea/test/tuplepack/access.cpp'
                break
        match = next((arm for arm in ('after', 'before')
                      if canonical and bases[arm]['files'].get(canonical) == digest), None)
        if match:
            omitted[relative.as_posix()] = {**row, 'source_arm': match,
                'source_path': canonical, 'archive_source_member': bases[match]['source_prefix'] + canonical}
        else:
            kept[relative.as_posix()] = row
    return kept, omitted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staging', type=Path, required=True)
    parser.add_argument('--context', type=Path)
    parser.add_argument('--preview', action='store_true')
    args = parser.parse_args()
    bases, recovery = source_recovery()
    kept, omitted = select(bases)
    totals = Counter()
    for name, row in kept.items():
        group = next((p for p in DIRECTORIES if name.startswith(str(BUILD / p) + '/')), 'helpers-and-context')
        for suffix, amount in (('files', 1), ('bytes', row['bytes']), ('objects', name.endswith('.o')),
                               ('sources', Path(name).suffix in ('.h', '.cpp', '.py', '.sh'))):
            totals[group + '/' + suffix] += amount
    summary = {'selected_files': len(kept), 'selected_raw_bytes': sum(v['bytes'] for v in kept.values()),
        'objects': sum(n.endswith('.o') for n in kept),
        'sources': sum(Path(n).suffix in ('.h', '.cpp', '.py', '.sh') for n in kept),
        'omitted_source_files': len(omitted), 'omitted_source_bytes': sum(v['bytes'] for v in omitted.values()),
        'by_root': dict(totals)}
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    if args.preview:
        return
    assert args.context and not args.staging.exists(), 'use a fresh ignored staging directory'
    assert args.staging.resolve().is_relative_to((ROOT / 'build').resolve())
    for name, row in kept.items():
        destination = args.staging / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, destination)
        assert sha(destination) == sha(ROOT / name) == row['sha256']
        destination.chmod(0o555 if destination.stat().st_mode & 0o111 else 0o444)
    aliases = {str(BUILD / 'predicate-projection-final-v5-sources' / arm):
               {'source_arm': arm, 'source_manifest_sha256': bases[arm]['source_manifest_sha256'],
                'recovery_prefix': bases[arm]['source_prefix']} for arm in ('before', 'after')}
    manifest = {'scope': __doc__, 'files': kept, 'omitted_sources': omitted, 'summary': summary,
        'source_recovery': recovery, 'recoverable_source_aliases': aliases,
        'selection_script_sha256': sha(Path(__file__)),
        'exclusions': ['Mutable allocator-callers-final-integration and all CMake build/cache trees.',
            'Native ELFs, timing files and complete source trees already in worker archives.',
            'Older intermediate source trees and proof outputs outside the explicit seven-root selection.'],
        'recovery': 'artifacts.py fetch handle-followup-artifact.json FRESH; original repository-relative '
                    'paths are preserved. Reconstruct omitted sources from the named native source archive.'}
    save(args.staging / 'recovery-manifest.json', manifest)
    context = {'scope': __doc__, 'summary': summary, 'staging': str(args.staging),
        'recovery_manifest_sha256': sha(args.staging / 'recovery-manifest.json'),
        'source_recovery': recovery, 'recoverable_source_aliases': aliases,
        'artifact_reference': str(EVIDENCE / 'handle-followup-artifact.json'),
        'selection_script_sha256': sha(Path(__file__)), 'artifact_tool_sha256': sha(ROOT / 'workbench/tools/artifacts.py'),
        'local_directories_preserved': [str(BUILD / name) for name in DIRECTORIES],
        'cleanup_status': 'No deletion or pin release. Root coordinates subsequent cleanup. The seven selected '
                          'output roots and final-v5 source aliases are recoverable; unrelated intermediates '
                          'and mutable final integration are not admitted by this reference.',
        'limits': 'Durable recovery only; no new correctness, timing or performance result.'}
    save(args.context, context)


if __name__ == '__main__':
    main()
