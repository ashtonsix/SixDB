#!/bin/bash
set -euo pipefail

phase=workbench/spikes/frame-phase-1/lifecycle
mkdir -p "$SIXDB_RESULTS/inputs"
python3 - "$phase/native-inputs.json" "$SIXDB_RESULTS/input-artifact.json" <<'PY'
from pathlib import Path
import json,sys
Path(sys.argv[2]).write_text(json.dumps(json.loads(Path(sys.argv[1]).read_text())['artifact']))
PY
python3 workbench/tools/artifacts.py fetch "$SIXDB_RESULTS/input-artifact.json" "$SIXDB_RESULTS/recovered-inputs" \
  --file inputs/p__mbar.u64 --file inputs/EventTime-mid.u64 --file inputs/UserID.u64 --file inputs/URLHash.u64 \
  > "$SIXDB_RESULTS/input-fetch.txt" 2>&1
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"

for isa in avx2 avx512; do
  march=x86-64-v3
  if [ "$isa" = avx512 ]; then march=znver5; fi
  build="build/clang/frame-native-$isa"
  out="$SIXDB_RESULTS/$isa"
  mkdir -p "$out"
  python3 - "$isa" <<'PY'
from pathlib import Path
import sys
flags=set(Path('/proc/cpuinfo').read_text().split('flags',1)[1].split('\n',1)[0].split())
assert 'avx2' in flags
if sys.argv[1]=='avx512':assert {'avx512f','avx512bw','avx512vl','avx512vbmi'} <= flags
PY
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=frame-phase-1 -DSIXDB_MARCH="$march" -DSIXDB_TUNE=zen5 \
    > "$out/configure.txt" 2>&1
  cmake --build "$build" --target frame_phase_measure frame_phase_material_check \
    frame_phase_repair_check frame_phase_batch_check frame_measure_order_check \
    -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  clang++-21 -march="$march" -mtune=znver5 -dM -E -x c++ /dev/null > "$out/compiler-features.txt"
  python3 "$phase/native.py" --build "$build" --inputs "$SIXDB_RESULTS/recovered-inputs/inputs" \
    --output "$out" --cpu "$SIXDB_CPU" --isa "$isa"
done
