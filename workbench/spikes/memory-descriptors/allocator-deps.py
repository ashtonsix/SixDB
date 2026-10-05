#!/usr/bin/env python3
"""Fetch explicit benchmark competitors; hash pins also work offline from cache."""
import hashlib
import json
from pathlib import Path
import sys
import tarfile
import urllib.request

PACKAGES = [
    ("mimalloc", "v3.5.3", "mimalloc-3.5.3",
     "3b4a15153a59905995f7070296ed604bb5ccc00cabb8b93446931aff77224d47"),
    ("snmalloc", "0.7.5", "snmalloc-0.7.5",
     "6efb0daa33fd1c0b406915786041fbce7176562cdfe74aa68baf90a77c451619"),
]

def main():
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    receipt = []
    for name, tag, directory, expected in PACKAGES:
        url = f"https://codeload.github.com/microsoft/{name}/tar.gz/refs/tags/{tag}"
        archive = destination / (name + ".tar.gz")
        if not archive.exists():
            archive.write_bytes(urllib.request.urlopen(url, timeout=60).read())
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != expected:
            raise RuntimeError(f"{name}: source archive hash mismatch")
        if not (destination / directory).exists():
            with tarfile.open(archive) as source:
                source.extractall(destination, filter="data")
        receipt.append(dict(name=name, tag=tag, url=url, sha256=digest,
                            archive_bytes=archive.stat().st_size))
    print(json.dumps(receipt, indent=2))

if __name__ == "__main__":
    main()
