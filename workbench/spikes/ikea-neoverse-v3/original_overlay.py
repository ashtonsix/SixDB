"""Recover original candidate headers from a hash-verified source capture."""
import hashlib
import json
from pathlib import Path
import sys
import tarfile
archive, output = map(Path, sys.argv[1:])
ref = json.loads(Path(__file__).with_name('original-source.json').read_text())
assert hashlib.sha256(archive.read_bytes()).hexdigest() == ref['sha256']
with tarfile.open(archive) as tar:
    for member in ref['headers']:
        destination = output / Path(member).relative_to('ikea/include')
        destination.parent.mkdir(parents=True, exist_ok=True)
        data = tar.extractfile(member).read()
        if not destination.exists() or destination.read_bytes() != data:
            destination.write_bytes(data)
