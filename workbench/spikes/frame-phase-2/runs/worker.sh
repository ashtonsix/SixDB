#!/bin/bash
# Run boundaries as composed stages on a worker: byte census and the read
# probe over phase 1's real columns, one build per ISA, three process repeats.
# Usage: worker.py run workbench/spikes/frame-phase-2/runs/worker.sh --machine zen5|neoverse-v2
set -euo pipefail
inputs=workbench/spikes/frame-phase-1/encoding/worker-inputs.json
cp "$inputs" "$SIXDB_RESULTS/input-artifact.json"
python3 workbench/tools/artifacts.py fetch "$SIXDB_RESULTS/input-artifact.json" "$SIXDB_RESULTS/in" \
  > "$SIXDB_RESULTS/input-fetch.txt" 2>&1
cols="$SIXDB_RESULTS/in/inputs/cols"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
case "$(uname -m)" in
  x86_64) isas="${ISAS:-avx512}" ;;
  *) isas="${ISAS:-native}" ;;
esac
columns="OS RegionID ResolutionWidth UserID ClientIP"
for isa in $isas; do
  case "$isa" in
    avx2) flags="-DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5" ;;
    avx512) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5" ;;
    *) flags="" ;;
  esac
  build="build/clang/frame-runs-$isa"
  out="$SIXDB_RESULTS/$isa"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release '-DSIXDB_SPIKES=frame-phase-1;frame-phase-2' $flags \
    > "$out/configure.txt" 2>&1
  cmake --build "$build" --target frame_runs_census frame_runs_read frame_runs_pipelines -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  "$build/workbench/spikes/frame-phase-2/runs/frame_runs_census" "$cols" 2999808 $columns > "$out/census.jsonl"
  for repeat in 1 2 3; do
    taskset -c "$SIXDB_CPU" "$build/workbench/spikes/frame-phase-2/runs/frame_runs_pipelines" "$cols" 2999808 "${ROUNDS:-11}" $columns EventTime URLHash WatchID \
      | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/pipelines.jsonl"
    [ -n "${PIPELINES_ONLY:-}" ] && continue
    taskset -c "$SIXDB_CPU" "$build/workbench/spikes/frame-phase-2/runs/frame_runs_read" "$cols" 2999808 "${ROUNDS:-11}" $columns \
      | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/read.jsonl"
  done
done
