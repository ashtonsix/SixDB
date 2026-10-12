#!/usr/bin/env python3
"""Fetch the actual pinned read baseline; no copied descriptor implementation."""
import hashlib
from pathlib import Path
import tarfile
import urllib.request

REVISION = "6ddac802ffa9bcfbcc3f5f0d71de5dff9b0bc250"
SHA256 = "42c80948c1eb071ad9e49aa86310fd4ed616afc17c1cbeb3ac4f803aa9e438f9"
SZ_REVISION = "86d73f1021e6cce022c3cceb1f7837eb50338774"
SZ_SHA256 = "7b5340c3d09d9c7b2b342d00d5175e67b647754980bcaaaf83013473c306294e"


def main():
    cache = Path("build/streampack-deps")
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / "duckdb-6ddac802.tar.gz"
    if not archive.exists():
        urllib.request.urlretrieve(
            f"https://codeload.github.com/duckdb/duckdb/tar.gz/{REVISION}", archive)
    if hashlib.file_digest(archive.open("rb"), "sha256").hexdigest() != SHA256:
        raise SystemExit("DuckDB archive hash mismatch")
    prefix = f"duckdb-{REVISION}"
    with tarfile.open(archive) as source:
        entries = [m for m in source.getmembers()
                   if m.name.startswith(prefix + "/src/include/") or m.name == prefix + "/LICENSE"]
        source.extractall(cache, members=entries, filter="data")
    print(cache / prefix / "src/include")
    archive = cache / f"stringzilla-{SZ_REVISION}.tar.gz"
    if not archive.exists():
        urllib.request.urlretrieve(
            f"https://codeload.github.com/ashvardanian/StringZilla/tar.gz/{SZ_REVISION}", archive)
    if hashlib.file_digest(archive.open("rb"), "sha256").hexdigest() != SZ_SHA256:
        raise SystemExit("StringZilla archive hash mismatch")
    prefix = f"StringZilla-{SZ_REVISION}"
    with tarfile.open(archive) as source:
        entries = [m for m in source.getmembers()
                   if m.name.startswith(prefix + "/include/") or m.name == prefix + "/LICENSE"]
        source.extractall(cache, members=entries, filter="data")
    print(cache / prefix / "include")


if __name__ == "__main__":
    main()
