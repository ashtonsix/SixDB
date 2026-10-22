#!/bin/bash
# Encoding driver on a worker: real columns from the input artifact, one build
# per ISA, every column under its default options plus a few option sets.
# Usage: worker.py run workbench/spikes/ikea-frame/encoding/worker.sh --machine zen5|neoverse-v2
set -euo pipefail
enc=workbench/spikes/ikea-frame/encoding
python3 - "$enc/worker-inputs.json" "$SIXDB_RESULTS/input-artifact.json" <<'PY'
import json, sys
from pathlib import Path
Path(sys.argv[2]).write_text(json.dumps(json.loads(Path(sys.argv[1]).read_text())))
PY
python3 workbench/tools/artifacts.py fetch "$SIXDB_RESULTS/input-artifact.json" "$SIXDB_RESULTS/in" \
  > "$SIXDB_RESULTS/input-fetch.txt" 2>&1
in="$SIXDB_RESULTS/in/inputs"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
case "$(uname -m)" in
  x86_64) isas="${ISAS:-avx2 avx512}" ;;
  *) isas="${ISAS:-native}" ;;
esac
for isa in $isas; do
  case "$isa" in
    avx2) flags="-DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5" ;;
    avx512) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5" ;;
    *) flags="" ;;
  esac
  build="build/clang/frame-encoding-$isa"
  out="$SIXDB_RESULTS/$isa"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=ikea-frame $flags \
    > "$out/configure.txt" 2>&1
  cmake --build "$build" --target frame_encoding_bodies frame_encoding_borrowed_check \
    -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  bin="$build/$enc"
  # Semantic smoke on this ISA before timing.
  for c in OS URLHash; do
    "$bin/frame_encoding_borrowed_check" "$c" "$in/cols/$c.u64" --dictionary=sorted > /dev/null
  done
  "$bin/frame_encoding_borrowed_check" EventTime "$in/clustered/EventTime.u64" > /dev/null
  run() { taskset -c "$SIXDB_CPU" "$bin/frame_encoding_bodies" "$@" --no-bindings >> "$out/bodies.jsonl"; }
  for round in 1 2 3; do
    for c in OS ResolutionWidth RegionID URLHash ConnectTiming EventTime UserID ClientIP WatchID IsRefresh CounterID; do
      run "$c" "$in/cols/$c.u64"
    done
    run EventTime-clustered "$in/clustered/EventTime.u64"
    run URLHash-sorted "$in/cols/URLHash.u64" --dictionary=sorted
    run OS-sorted "$in/cols/OS.u64" --dictionary=sorted
    for f in "$in"/jena-0/*.u64; do run "jena-0/$(basename "$f" .u64)" "$f" --checkpoints=packed --delta-exceptions; done
    for f in "$in"/wide/*.u64; do run "wide/$(basename "$f" .u64)" "$f"; done
  done
done
echo done > "$SIXDB_RESULTS/status.txt"
