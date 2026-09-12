#!/usr/bin/env python3
"""Retain selected runs in Git with full, verified bundles in SixDB's S3 prefix."""

import argparse
from contextlib import nullcontext
from collections import Counter, defaultdict
import csv
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import shlex
import subprocess
import tarfile
import tempfile

from evidence import compact_run, compact_files, git_root, verify_exports
import storage

ROOT = Path(__file__).resolve().parents[2]
BUCKET = "calico-fleet-artifacts"
REGION = "us-east-1"


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def rebuildable(name, size, prefix):
    """Large compiler outputs; measurements, logs and arbitrary data stay local."""
    return size >= 1024 ** 2 and (Path(name).suffix in {'.asm', '.s'} or
        prefix.startswith((b'\x7fELF', b'!<arch>\n', b'BC\xc0\xde')) or
        (b'file format elf' in prefix and b'Disassembly of section ' in prefix))


def remote_manifest(reference):
    """Verify the entire S3 object and its members without storing a local bundle."""
    class Reader:
        def __init__(self, stream):
            self.stream, self.hash, self.size = stream, hashlib.sha256(), 0

        def read(self, size=-1):
            data = self.stream.read(size)
            self.hash.update(data)
            self.size += len(data)
            return data

    members = {}
    with tempfile.TemporaryFile() as errors:
        command = ['aws', 's3', 'cp', '--only-show-errors',
                   f"s3://{reference['bucket']}/{reference['key']}", '-', '--region', reference['region']]
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors) as process:
            reader = Reader(process.stdout)
            try:
                with tarfile.open(fileobj=reader, mode='r|gz') as archive:
                    for member in archive:
                        name = PurePosixPath(member.name)
                        if (not member.isfile() or not name.parts or name.is_absolute() or
                                '..' in name.parts or name.as_posix() in members):
                            raise ValueError(f'unsafe archive member: {member.name}')
                        digest, size, prefix = hashlib.sha256(), 0, b''
                        with archive.extractfile(member) as source:
                            for chunk in iter(lambda: source.read(1024 ** 2), b''):
                                if size == 0:
                                    prefix = chunk[:512]
                                digest.update(chunk)
                                size += len(chunk)
                        if size != member.size:
                            raise ValueError(f'truncated archive member: {name}')
                        members[name.as_posix()] = {'bytes': size, 'sha256': digest.hexdigest(),
                            'rebuildable': rebuildable(name, size, prefix)}
                while reader.read(1024 ** 2):
                    pass
            except BaseException:
                process.kill()
                raise
            if process.wait():
                errors.seek(0)
                raise RuntimeError(errors.read().decode(errors='replace').strip())
    if (reader.size != reference['bytes'] or reader.hash.hexdigest() != reference['sha256'] or
            len(members) != reference['files']):
        raise ValueError('remote bundle size/SHA-256/file count mismatch; local data preserved')
    return members


def files(directory):
    result = []
    for path in sorted(directory.rglob("*")):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError(f"bundle only regular files and directories: {path}")
        if path.is_file():
            result.append(path)
    return result


def verify_run(directory, *, archived=None):
    receipt = directory / "run.json"
    if not receipt.exists():
        return  # A validation/log bundle can be retained without being a run.
    data = json.loads(receipt.read_text())
    if data["status"] not in {"complete", "failed"}:
        raise ValueError("cannot retain an unfinished run")
    for name, expected in data["artifact_sha256"].items():
        path = directory / name
        if not path.resolve().is_relative_to(directory.resolve()):
            raise ValueError(f'run artifact is outside its directory: {name}')
        actual = sha256(path) if archived is None or path.is_file() else archived.get(name, {}).get('sha256')
        if actual != expected:
            raise ValueError(f"run artifact missing or changed: {name}")


def pack(directory, output, *, validate_run=True):
    if validate_run:
        verify_run(directory)
    members = files(directory)
    if not members:
        raise ValueError("empty bundle")
    before = {str(p.relative_to(directory)): sha256(p) for p in members}
    with output.open("wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=0) as zipped:
        with tarfile.open(fileobj=zipped, mode="w|") as archive:
            for path in members:
                info = tarfile.TarInfo(str(path.relative_to(directory)))
                info.size = path.stat().st_size
                info.mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
                with path.open("rb") as handle:
                    archive.addfile(info, handle)
    if before != {str(p.relative_to(directory)): sha256(p) for p in files(directory)}:
        raise ValueError("bundle changed while packing")
    return len(members)


def aws(*args, region=REGION):
    result = subprocess.run(["aws", "s3api", *args, "--region", region, "--no-cli-pager"],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return result


def download(reference, destination):
    result = aws("get-object", "--bucket", reference["bucket"], "--key", reference["key"],
                 str(destination), region=reference["region"])
    if result.returncode:
        raise RuntimeError(result.stderr.strip())
    if destination.stat().st_size != reference["bytes"] or sha256(destination) != reference["sha256"]:
        raise ValueError("downloaded bundle does not match its recorded size/SHA-256")


def publish_inputs(directory):
    """Record prepared dependencies without embedding them in each retained run."""
    import datasets
    inputs = json.loads((directory / 'run.json').read_text()).get('inputs', {})
    references = {}
    for mount, entry in inputs.items():
        p = Path(mount)
        if not p.parts or p.is_absolute() or '..' in p.parts:
            raise ValueError('Invalid input mount')
        path = directory / mount
        if not (path / 'prepared.json').exists():
            path = Path(entry['path'])
        meta = datasets.verify(path)
        if meta['id'] != entry['id'] or meta['key'] != entry['key']:
            raise ValueError('Run input no longer matches its recorded identity')
        references[mount] = datasets.publish(path)
    return references


def restore_inputs(directory):
    """Resolve a run's prepared dependencies, also after a worker bundle is fetched."""
    refs = directory / 'input-artifacts.json'
    if not refs.exists():
        return
    import datasets
    expected = json.loads((directory / 'run.json').read_text()).get('inputs', {})
    references = json.loads(refs.read_text())
    if references.keys() != expected.keys():
        raise ValueError('Input references do not match the run')
    for mount, ref in references.items():
        p = Path(mount)
        if not p.parts or p.is_absolute() or '..' in p.parts:
            raise ValueError('Invalid restored input mount')
        if any(ref[k] != expected[mount][k] for k in ('id', 'key')):
            raise ValueError('Restored dependency has a different identity')
        target = directory / p
        if target.exists():
            if datasets.verify(target)['id'] != ref['id']:
                raise ValueError('Existing restored input differs')
        else:
            source = datasets.restore(ref)
            target.parent.mkdir(parents=True, exist_ok=True)
            with storage.transfer_lock():
                storage.require_space(target, sum(path.stat().st_size for path in files(source)))
                with tempfile.TemporaryDirectory(dir=target.parent, prefix='.input-') as temp:
                    staging = Path(temp) / 'input'
                    shutil.copytree(source, staging)
                    storage.sync_tree(staging)
                    staging.rename(target)
                    storage.sync_directory(target.parent)


def publish(directory, temporary, *, bucket=BUCKET, region=REGION, validate_run=True):
    # Inputs are independent objects. Add a restoration manifest to the bundle
    # without rewriting the completed run or copying datasets into every run.
    receipt = directory / 'run.json'
    if validate_run and receipt.exists():
        inputs = json.loads(receipt.read_text()).get('inputs', {})
        if inputs:
            staged = temporary / 'with-input-references'
            staged.mkdir()
            mounts = [Path(m) for m in inputs]
            if any(not m.parts or m.is_absolute() or '..' in m.parts for m in mounts):
                raise ValueError('Invalid input mount')
            for path in files(directory):
                name = path.relative_to(directory)
                if name == Path('input-artifacts.json') or any(name.is_relative_to(m) for m in mounts):
                    continue
                (staged / name).parent.mkdir(parents=True, exist_ok=True)
                os.link(path, staged / name)
            references = publish_inputs(directory)
            (staged / 'input-artifacts.json').write_text(json.dumps(references, indent=2, sort_keys=True) + '\n')
            directory = staged
    bundle = temporary / "bundle.tar.gz"
    count = pack(directory, bundle, validate_run=validate_run)
    checksum = sha256(bundle)
    key = f"sixdb/artifacts/sha256/{checksum}.tar.gz"
    reference = {"format": 1, "uri": f"s3://{bucket}/{key}", "bucket": bucket,
                 "region": region, "key": key, "sha256": checksum,
                 "bytes": bundle.stat().st_size, "files": count}
    if not validate_run:
        reference['kind'] = 'files'  # Generic script output has no required receipt schema.
    print(f"Upload: {reference['uri']}", flush=True)
    # A retry cannot replace an existing object. Always verify by downloading
    # the actual bytes, including after a timeout with an uncertain outcome.
    result = aws("put-object", "--bucket", bucket, "--key", key, "--body", str(bundle),
                 "--if-none-match", "*", "--content-type", "application/gzip", region=region)
    try:
        download(reference, temporary / "verified.tar.gz")
    except Exception as exc:
        raise RuntimeError(f"bundle not verified; local data preserved. {result.stderr.strip()}") from exc
    print(f"Verified download: {reference['bytes']:,} bytes, {count} files", flush=True)
    return reference


def install(staging, destination):
    if destination.exists():
        previous = {str(p.relative_to(destination)): sha256(p) for p in files(destination)}
        current = {str(p.relative_to(staging)): sha256(p) for p in files(staging)}
        if previous != current:
            raise ValueError(f"destination differs; choose a new evidence directory: {destination}")
        return
    staging.rename(destination)


def prepare_compact(source, staging, selected, regenerate):
    if selected is None:
        compact_run(source, staging)
    else:
        receipt = json.loads((source / 'run.json').read_text())
        receipt['compact'] = {'files': selected, 'regenerate': regenerate or []}
        compact_files(source, staging, receipt)
    if regenerate is not None:
        path = staging / 'provenance.json'
        meta = json.loads(path.read_text())
        meta['regenerate'] = regenerate
        path.write_text(json.dumps(meta, indent=2, sort_keys=True) + '\n')


def preview_export(staging, destination):
    """Describe actual compact bytes and ignore rules; size is information, not a gate."""
    members = sorted(files(staging), key=lambda p: (-p.stat().st_size, p.as_posix()))
    names = [p.relative_to(staging).as_posix() for p in members]
    root = git_root(destination)
    ignored = set()
    if root is not None:
        paths = [(destination / name).relative_to(root).as_posix() for name in names + ['artifact.json']]
        result = subprocess.run(['git', '-C', str(root), 'check-ignore', '--no-index', '-z', '--stdin'],
                                input=('\0'.join(paths) + '\0').encode(), capture_output=True)
        if result.returncode not in (0, 1):
            raise RuntimeError(result.stderr.decode())
        ignored = set(result.stdout.decode().rstrip('\0').split('\0')) if result.stdout else set()
    print(f'Git export: {destination}')
    lines = total_bytes = 0
    by_name = Counter()
    copies = defaultdict(list)
    for path, name in zip(members, names):
        data = path.read_bytes()
        count = None if b'\0' in data else len(data.splitlines())
        total_bytes += len(data)
        lines += count or 0
        by_name[path.name] += len(data)
        copies[hashlib.sha256(data).hexdigest()].append(name)
        relative = (destination / name).relative_to(root).as_posix() if root else ''
        warning = '  IGNORED by Git' if relative in ignored else ''
        print(f'  {len(data):>9,} bytes {str(count) if count is not None else "binary":>7} lines  {name}{warning}')
        if path.suffix == '.csv':
            coverage = csv_coverage(path)
            if coverage:
                print(f'    {coverage}')
    print(f'  {len(members)} files, {total_bytes:,} bytes, {lines:,} text lines; plus artifact.json after upload')
    if len(by_name) < len(members):
        print('  Across directories: ' + '; '.join(f'{name} {size:,} bytes'
                                                 for name, size in by_name.most_common(4)))
    for group in copies.values():
        if len(group) > 1:
            print('  Identical bytes: ' + ' = '.join(group))
    print('  Keep inputs for the findings and complete comparisons; full diagnostics can stay in the bundle.')
    print('  Selection examples: workbench/tools/artifacts.md')
    for name in sorted(ignored):
        print(f'  Ignored export: {name}')
    return ignored


def csv_coverage(path):
    """Describe named CSV rows without interpreting their units or choosing winners."""
    try:
        with path.open(newline='', encoding='utf-8-sig') as handle:
            reader = csv.DictReader(handle)
            key = next((k for k in ('name', 'case') if k in (reader.fieldnames or [])), None)
            if key is None:
                return None
            counts = Counter(row[key] for row in reader if row.get(key))
    except (UnicodeError, csv.Error):
        return None  # Preview remains useful for arbitrary non-benchmark CSVs.
    families = Counter()
    for name, count in counts.items():
        families[name.split('/', 1)[0]] += count
    detail = '; '.join(f'{name} {count:,}' for name, count in families.most_common(4))
    if len(families) > 4:
        detail += f'; {len(families) - 4} more prefixes'
    return f'{len(counts):,} distinct names, {sum(counts.values()):,} named rows by first path component: {detail}'


def preview(source, destination, selected=None, regenerate=None):
    with tempfile.TemporaryDirectory(prefix='sixdb-retention-preview-') as name:
        staging = Path(name)
        prepare_compact(source.resolve(), staging, selected, regenerate)
        return preview_export(staging, destination.resolve())


def worker_reference(source):
    """Reuse a worker's complete archive even when local compiler output is absent."""
    for parent in source.parents:
        receipt_path = parent / 'collection.json'
        if not receipt_path.is_file() or not source.is_relative_to(parent / 'results'):
            continue
        receipt = json.loads(receipt_path.read_text())
        reference = receipt['artifact']
        prefix = source.relative_to(parent / 'results').as_posix()
        prefix = '' if prefix == '.' else prefix + '/'
        members = {name.removeprefix(prefix): entry for name, entry in remote_manifest(reference).items()
                   if name.startswith(prefix)}
        if not members:
            raise ValueError('study is not present in the verified worker archive')
        for path in files(source):
            name = path.relative_to(source).as_posix()
            if name in members and sha256(path) != members[name]['sha256']:
                raise ValueError(f'local worker output differs from archived output: {name}')
        verify_run(source, archived=members)
        print('Reuse verified worker archive; no duplicate upload.', flush=True)
        return reference | {'subdirectory': prefix.rstrip('/'), 'kind': 'run'}
    return None


def retain(source, destination, selected=None, regenerate=None):
    source, destination = source.resolve(), destination.resolve()
    if destination.is_relative_to(source):
        raise ValueError("keep retained evidence outside its input run")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix=".retain-") as name:
        temporary = Path(name)
        staging = temporary / "evidence"
        staging.mkdir()
        prepare_compact(source, staging, selected, regenerate)
        if preview_export(staging, destination):
            raise ValueError('Compact export contains Git-ignored files; rename the selected output or adjust its scoped ignore rule')
        reference = worker_reference(source) or publish(source, temporary)
        (staging / "artifact.json").write_text(json.dumps(reference, indent=2) + "\n")
        install(staging, destination)
    print(f"Retained: {destination}")


def selection(selected):
    if selected is not None:
        names = [PurePosixPath(name) for name in selected]
        if not names or any(not name.parts or name.is_absolute() or '..' in name.parts for name in names):
            raise ValueError('select relative file paths within the bundle')
        selected = {name.as_posix() for name in names}
        if len(selected) != len(names):
            raise ValueError('select distinct files')
    return selected


def unpack(bundle, destination, *, selected=None, manifest=None, omit_compiler=False, subdirectory=''):
    selected = selection(selected)
    scope = PurePosixPath(subdirectory)
    if scope.is_absolute() or '..' in scope.parts:
        raise ValueError('bundle subdirectory must be a relative path')
    if selected is not None and omit_compiler:
        raise ValueError('choose exact files or omit compiler output, not both')
    with tarfile.open(bundle) as archive:
        seen, scoped = set(), set()
        for member in archive:
            name = PurePosixPath(member.name)
            if not member.isfile() or not name.parts or name.is_absolute() or ".." in name.parts or name.as_posix() in seen:
                raise ValueError(f"unsafe archive member: {member.name}")
            seen.add(name.as_posix())
            if not name.is_relative_to(scope):
                continue
            name = name.relative_to(scope)
            if not name.parts:
                raise ValueError('bundle subdirectory names a file')
            scoped.add(name.as_posix())
            source = archive.extractfile(member)
            if source is None:
                raise ValueError(f'archive member has no file contents: {member.name}')
            with source:
                prefix = source.read(512)
                compiler = rebuildable(name, member.size, prefix)
                keep = name.as_posix() in selected if selected is not None else not (omit_compiler and compiler)
                path = destination.joinpath(*name.parts)
                if keep:
                    storage.require_space(destination, member.size)
                    path.parent.mkdir(parents=True, exist_ok=True)
                digest, size = hashlib.sha256(), 0
                # Hash omitted members without ever expanding them onto disk.
                with (path.open('xb') if keep else nullcontext()) as target:
                    chunk = prefix
                    while chunk:
                        digest.update(chunk)
                        size += len(chunk)
                        if target is not None:
                            target.write(chunk)
                        chunk = source.read(1024 ** 2)
                if size != member.size:
                    raise ValueError(f'truncated archive member: {name}')
            if keep:
                path.chmod(member.mode & 0o777)
            if manifest is not None:
                manifest[name.as_posix()] = {'bytes': member.size, 'sha256': digest.hexdigest(),
                                           'rebuildable': compiler}
    if selected is not None and selected - scoped:
        raise ValueError(f"selected files missing from bundle: {', '.join(sorted(selected - scoped))}")
    if subdirectory and not scoped:
        raise ValueError('bundle subdirectory is empty or missing')
    return len(seen)


def restore_bundle(reference, destination, *, selected=None, omit_compiler=False):
    """Restore chosen files durably; return hashes for every in-scope member.

    Exact selections skip whole-run validation. Compiler omissions instead
    validate missing run members against the verified archive's hashes.
    """
    with storage.transfer_lock():
        return _restore_bundle(reference, destination, selected=selected, omit_compiler=omit_compiler)


def _restore_bundle(reference, destination, *, selected=None, omit_compiler=False):
    destination = destination.resolve()
    if destination.exists():
        raise ValueError("fetch destination already exists")
    destination.parent.mkdir(parents=True, exist_ok=True)
    storage.require_space(destination, reference['bytes'])
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix=".fetch-") as name:
        temporary = Path(name)
        bundle = temporary / "bundle.tar.gz"
        download(reference, bundle)
        staging = temporary / "run"
        staging.mkdir()
        manifest = {}
        count = unpack(bundle, staging, selected=selected, manifest=manifest, omit_compiler=omit_compiler,
                       subdirectory=reference.get('subdirectory', ''))
        if count != reference["files"]:
            raise ValueError("bundle file count mismatch")
        if selected is None and reference.get('kind') != 'files':
            verify_run(staging, archived=manifest if omit_compiler else None)
        storage.sync_tree(staging)
        staging.rename(destination)
        storage.sync_directory(destination.parent)
        return manifest


def fetch(reference_path, destination, *, selected=None):
    if reference_path.is_dir():
        reference_path = reference_path / "artifact.json"
    reference = json.loads(reference_path.read_text())
    destination = destination.resolve()
    if not destination.is_relative_to(ROOT / 'build'):
        raise ValueError("fetch needs a new directory under this checkout's build/, "
                         'for example build/recovered/NAME; SIXDB_DATA_CACHE is for prepared inputs')
    if destination.exists():
        raise ValueError('fetch destination already exists')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temp:
        staging = Path(temp) / 'run'
        restore_bundle(reference, staging, selected=selected)
        if selected is None:
            restore_inputs(staging)
        staging.rename(destination)
    if selected is None:
        print(f"Restored and verified: {destination}")
    else:
        print(f"Partial recovery: {len(selected)} selected files from verified bundle: {destination}")
        print('Whole-run validation and input dataset restoration were not requested.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, help_text in [('retain', 'upload a completed run and write compact Git evidence'),
                            ('preview', 'show compact sizes and ignored files without uploading or installing')]:
        selection_parser = commands.add_parser(name, help=help_text)
        selection_parser.add_argument('run', type=Path)
        selection_parser.add_argument('evidence', type=Path)
        selection_parser.add_argument('--file', action='append', dest='selected', help='Keep this file byte-for-byte; repeat as needed')
        selection_parser.add_argument('--regenerate', help='Record an offline table-regeneration command; {evidence} denotes its directory')
    verify_parser = commands.add_parser('verify', help='verify compact evidence under a directory, including Git-only membership')
    verify_parser.add_argument('evidence', type=Path)
    tree = verify_parser.add_mutually_exclusive_group()
    tree.add_argument('--staged', action='store_const', const=':', dest='tree', help='read only staged Git blobs')
    tree.add_argument('--tree', help='read only blobs from this Git revision (for example HEAD)')
    put_parser = commands.add_parser("put", help="upload a validation/other bundle and write its reference")
    put_parser.add_argument("directory", type=Path)
    put_parser.add_argument("reference", type=Path)
    fetch_parser = commands.add_parser("fetch", help="download and verify a reference or evidence directory")
    fetch_parser.add_argument("reference", type=Path)
    fetch_parser.add_argument("output", type=Path,
                              help="new directory under this checkout's build/, e.g. build/recovered/NAME")
    fetch_parser.add_argument('--file', action='append', dest='selected',
                              help='Restore only this exact bundle file; repeat as needed (no input dataset restoration)')
    args = parser.parse_args()
    if args.command in ('retain', 'preview'):
        action = retain if args.command == 'retain' else preview
        action(args.run, args.evidence, args.selected, shlex.split(args.regenerate) if args.regenerate else None)
    elif args.command == 'verify':
        count = verify_exports(args.evidence, tree=args.tree)
        print(f'Verified {count} compact exports ({args.tree or "worktree"})')
    elif args.command == "fetch":
        fetch(args.reference, args.output, selected=args.selected)
    else:
        if args.reference.resolve().is_relative_to(args.directory.resolve()):
            parser.error("keep the reference outside its input bundle")
        cache = ROOT / "build/artifacts"
        cache.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=cache) as name:
            reference = publish(args.directory.resolve(), Path(name))
        content = json.dumps(reference, indent=2) + "\n"
        if args.reference.exists() and args.reference.read_text() != content:
            raise ValueError("reference already exists with different content")
        args.reference.parent.mkdir(parents=True, exist_ok=True)
        args.reference.write_text(content)


if __name__ == "__main__":
    main()
