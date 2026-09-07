#!/usr/bin/env python3
"""Retain selected runs in Git with full, verified bundles in SixDB's S3 prefix."""

import argparse
import gzip
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import tempfile

from evidence import compact_run

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


def pack(directory, output):
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


def publish(directory, temporary):
    bundle = temporary / "bundle.tar.gz"
    count = pack(directory, bundle)
    checksum = sha256(bundle)
    key = f"sixdb/artifacts/sha256/{checksum}.tar.gz"
    reference = {"format": 1, "uri": f"s3://{BUCKET}/{key}", "bucket": BUCKET,
                 "region": REGION, "key": key, "sha256": checksum,
                 "bytes": bundle.stat().st_size, "files": count}
    print(f"Upload: {reference['uri']}", flush=True)
    # A retry cannot replace an existing object. Always verify by downloading
    # the actual bytes, including after a timeout with an uncertain outcome.
    result = aws("put-object", "--bucket", BUCKET, "--key", key, "--body", str(bundle),
                 "--if-none-match", "*", "--content-type", "application/gzip")
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


def retain(source, destination):
    source, destination = source.resolve(), destination.resolve()
    if destination.is_relative_to(source):
        raise ValueError("keep retained evidence outside its input run")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix=".retain-") as name:
        temporary = Path(name)
        staging = temporary / "evidence"
        staging.mkdir()
        compact_run(source, staging)
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
            with archive.extractfile(member) as source, path.open("xb") as target:
                shutil.copyfileobj(source, target)
            path.chmod(member.mode & 0o777)


def fetch(reference_path, destination):
    if reference_path.is_dir():
        reference_path = reference_path / "artifact.json"
    reference = json.loads(reference_path.read_text())
    destination = destination.resolve()
    if not destination.is_relative_to(ROOT / "build"):
        raise ValueError("fetch into ignored build/ output")
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
        verify_run(staging)
        staging.rename(destination)
    print(f"Restored and verified: {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    retain_parser = commands.add_parser("retain", help="upload a completed run and write compact Git evidence")
    retain_parser.add_argument("run", type=Path)
    retain_parser.add_argument("evidence", type=Path)
    put_parser = commands.add_parser("put", help="upload a validation/other bundle and write its reference")
    put_parser.add_argument("directory", type=Path)
    put_parser.add_argument("reference", type=Path)
    fetch_parser = commands.add_parser("fetch", help="download and verify a reference or evidence directory")
    fetch_parser.add_argument("reference", type=Path)
    fetch_parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.command == "retain":
        retain(args.run, args.evidence)
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
