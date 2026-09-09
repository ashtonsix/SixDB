#!/bin/bash
# Two invocations should reuse the UAP cache and the unchanged compiled TU.
set -euo pipefail
python3 - "$@" <<'PY'
import json
import os
from pathlib import Path
import subprocess
import sys
import time
sys.path.insert(0, 'workbench/tools')
import datasets
root = Path.cwd()
assert not (root / 'reuse-stale-file').exists(), 'old job left a file in the new source snapshot'
(root / 'reuse-stale-file').write_text('must disappear on the next source refresh')
data = datasets.get('uap-core')
build = root / 'build/worker-reuse-smoke'
source = build / 'source'
source.mkdir(parents=True, exist_ok=True)
for name, value in {
    'CMakeLists.txt': 'cmake_minimum_required(VERSION 3.24)\nproject(reuse LANGUAGES CXX)\nadd_executable(probe probe.cpp)\n',
    'probe.cpp': 'int main() { return 0; }\n',
}.items():
    path = source / name
    if not path.exists() or path.read_text() != value:
        path.write_text(value)
subprocess.run(['cmake', '-G', 'Ninja', '-S', str(source), '-B', str(build / 'objects'),
                '-DCMAKE_CXX_COMPILER=clang++-21'], check=True)
subprocess.run(['cmake', '--build', str(build / 'objects'), '-j', '1'], check=True)
subprocess.run([str(build / 'objects/probe')], check=True)
result = {'worker': os.environ['SIXDB_WORKER_ID'], 'reused': os.environ['SIXDB_WORKER_REUSED'],
          'dataset_key': datasets.verify(data)['key'], 'dataset_path': str(data),
          'dataset_mtime_ns': data.stat().st_mtime_ns,
          'binary_mtime_ns': (build / 'objects/probe').stat().st_mtime_ns}
(Path(os.environ['SIXDB_RESULTS']) / 'reuse.json').write_text(json.dumps(result, indent=2) + '\n')
time.sleep(float(sys.argv[1]) if len(sys.argv) > 1 else 0)
PY
