#!/bin/bash
# Paired complete-container histories. Only Orbital's two repaired files differ.
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
result_root=$SIXDB_RESULTS
source_root=$PWD
repair_input=workbench/spikes/hashtable/allocator-repair-input
python3 - "$repair_input" "$result_root" <<'PY'
from pathlib import Path
import hashlib,json,sys
expected={'internal.h':'36cc8c371b8d6ca0458ac4e024b232940001a3f8efa017b354d3c88bdfdc03e4',
          'window.cpp':'128c5c67474bd8ff39c2eb0e7650501197e45a7209d88689806dab418f28c1d4'}
for name,digest in expected.items():
    assert hashlib.sha256((Path(sys.argv[1])/name).read_bytes()).hexdigest()==digest
Path(sys.argv[2],'repair-input.json').write_text(json.dumps(expected,indent=2)+'\n')
PY
python3 workbench/tools/capture.py create build/vector-allocator-repaired --base . \
  --replace "orbital/src/allocation/internal.h=$repair_input/internal.h" \
  --replace "orbital/src/allocation/window.cpp=$repair_input/window.cpp" \
  > "$result_root/capture.txt"
cp build/vector-allocator-repaired.capture.json "$result_root/repaired-capture.json"
mkdir -p "$result_root/baseline" "$result_root/repaired"
SIXDB_RESULTS="$result_root/baseline" bash workbench/spikes/hashtable/vector-native.sh
# This source owns a new build directory, rebuilding every Orbital TU after its
# private record layout changed. No object from the baseline build is reused.
cd "$source_root/build/vector-allocator-repaired"
SIXDB_RESULTS="$result_root/repaired" bash workbench/spikes/hashtable/vector-native.sh
