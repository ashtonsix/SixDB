#!/usr/bin/env python3
"""Verify literal frozen baseline after undoing its namespace and common-kernel binding changes."""
from pathlib import Path
import hashlib
import subprocess
import sys
p=Path(__file__).with_name('sparse_before.h')
original=p.read_bytes().replace(b'namespace ikea::intervals_before {',b'namespace ikea::intervals {').replace(b'namespace bl = ikea::intervals;',b'namespace bl = ikea::boundlist;')
expected='36e9e9cb441dcda65e217dd4f5a5c177662cacedf3f1125f1c1df30747280d2b'
assert hashlib.sha256(original).hexdigest()==expected,'frozen baseline changed'
if '--git' in sys.argv:
    old=subprocess.check_output(['git','show','66743fdf:workbench/spikes/intervals/include/ikea/intervals/sparse.h'])
    assert old==original,'baseline differs from pinned source'
print('Frozen 66743fdf sparse source verified; only namespace and common-kernel binding differ')
