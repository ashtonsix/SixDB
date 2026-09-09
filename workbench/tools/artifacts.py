#!/usr/bin/env python3
"""Retain selected runs in Git with full, verified bundles in SixDB's S3 prefix."""

import argparse
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

ROOT = Path(__file__).resolve().parents[2]
BUCKET = "calico-fleet-artifacts"
REGION = "us-east-1"


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def files(directory):
    result = []
    for path in sorted(directory.rglob("*")):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError(f"bundle only regular files and directories: {path}")
        if path.is_file():
            result.append(path)
    return result


def verify_run(directory):
    receipt = directory / "run.json"
    if not receipt.exists():
        return  # A validation/log bundle can be retained without being a run.
    data = json.loads(receipt.read_text())
    if data["status"] not in {"complete", "failed"}:
        raise ValueError("cannot retain an unfinished run")
    for name, expected in data["artifact_sha256"].items():
        path = directory / name
        if not path.resolve().is_relative_to(directory.resolve()) or sha256(path) != expected:
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
            shutil.copytree(datasets.restore(ref), target)


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
    members = files(staging)
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
    for path, name in zip(members, names):
        data = path.read_bytes()
        count = None if b'\0' in data else len(data.splitlines())
        total_bytes += len(data)
        lines += count or 0
        relative = (destination / name).relative_to(root).as_posix() if root else ''
        warning = '  IGNORED by Git' if relative in ignored else ''
        print(f'  {len(data):>9,} bytes {str(count) if count is not None else "binary":>7} lines  {name}{warning}')
    print(f'  {len(members)} files, {total_bytes:,} bytes, {lines:,} text lines; plus artifact.json after upload')
    for name in sorted(ignored):
        print(f'  Ignored export: {name}')
    return ignored


def preview(source, destination, selected=None, regenerate=None):
    with tempfile.TemporaryDirectory(prefix='sixdb-retention-preview-') as name:
        staging = Path(name)
        prepare_compact(source.resolve(), staging, selected, regenerate)
        return preview_export(staging, destination.resolve())


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
        reference = publish(source, temporary)
        (staging / "artifact.json").write_text(json.dumps(reference, indent=2) + "\n")
        install(staging, destination)
    print(f"Retained: {destination}")


def unpack(bundle, destination):
    with tarfile.open(bundle) as archive:
        seen = set()
        for member in archive:
            name = PurePosixPath(member.name)
            if not member.isfile() or name.is_absolute() or ".." in name.parts or member.name in seen:
                raise ValueError(f"unsafe archive member: {member.name}")
            seen.add(member.name)
            path = destination.joinpath(*name.parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise ValueError(f'archive member has no file contents: {member.name}')
            with source, path.open("xb") as target:
                shutil.copyfileobj(source, target)
            path.chmod(member.mode & 0o777)


def restore_bundle(reference, destination):
    """Restore the verified bytes; prepared-input resolution is a separate step."""
    destination = destination.resolve()
    if destination.exists():
        raise ValueError("fetch destination already exists")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix=".fetch-") as name:
        temporary = Path(name)
        bundle = temporary / "bundle.tar.gz"
        download(reference, bundle)
        staging = temporary / "run"
        staging.mkdir()
        unpack(bundle, staging)
        if len(files(staging)) != reference["files"]:
            raise ValueError("bundle file count mismatch")
        if reference.get('kind') != 'files':
            verify_run(staging)
        staging.rename(destination)


def fetch(reference_path, destination):
    if reference_path.is_dir():
        reference_path = reference_path / "artifact.json"
    reference = json.loads(reference_path.read_text())
    destination = destination.resolve()
    if not destination.is_relative_to(ROOT / 'build'):
        raise ValueError('fetch into ignored build/ output')
    if destination.exists():
        raise ValueError('fetch destination already exists')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temp:
        staging = Path(temp) / 'run'
        restore_bundle(reference, staging)
        restore_inputs(staging)
        staging.rename(destination)
    print(f"Restored and verified: {destination}")


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
    fetch_parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.command in ('retain', 'preview'):
        action = retain if args.command == 'retain' else preview
        action(args.run, args.evidence, args.selected, shlex.split(args.regenerate) if args.regenerate else None)
    elif args.command == 'verify':
        count = verify_exports(args.evidence, tree=args.tree)
        print(f'Verified {count} compact exports ({args.tree or "worktree"})')
    elif args.command == "fetch":
        fetch(args.reference, args.output)
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
