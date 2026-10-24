#!/bin/bash
# The bounds stage on a worker: bytes census against the exact oracle and
# encode-side ns per block, over phase 1's real columns, one build per ISA,
# three process repeats.
# Usage: worker.py run workbench/spikes/frame-phase-2/bounds/worker.sh --machine zen5|neoverse-v2
set -euo pipefail
inputs=workbench/spikes/frame-phase-1/encoding/worker-inputs.json
cp "$inputs" "$SIXDB_RESULTS/input-artifact.json"
python3 workbench/tools/artifacts.py fetch "$SIXDB_RESULTS/input-artifact.json" "$SIXDB_RESULTS/in" \
  > "$SIXDB_RESULTS/input-fetch.txt" 2>&1
cols="$SIXDB_RESULTS/in/inputs/cols"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
case "$(uname -m)" in
  x86_64) isas="${ISAS:-avx512 avx2}" ;;
  *) isas="${ISAS:-native}" ;;
esac
columns="ClientIP ConnectTiming CounterID EventTime IsRefresh OS RegionID ResolutionWidth URLHash UserID WatchID"
for isa in $isas; do
  case "$isa" in
    avx2) flags="-DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5" ;;
    avx512) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5" ;;
    *) flags="" ;;
  esac
  build="build/clang/frame-bounds-$isa"
  out="$SIXDB_RESULTS/$isa"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release '-DSIXDB_SPIKES=frame-phase-1;frame-phase-2' $flags \
    > "$out/configure.txt" 2>&1
  cmake --build "$build" --target frame_bounds_census frame_bounds_time frame_bounds_parts frame_bounds_compose -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  bin="$build/workbench/spikes/frame-phase-2/bounds"
  [ "$isa" = "${isas%% *}" ] && "$bin/frame_bounds_census" "$cols" 2999808 $columns gen_low gen_two gen_heavy gen_bimodal gen_shift gen_ts gen_rand64 > "$out/census.jsonl"
  for repeat in 1 2 3; do
    for col in OS ResolutionWidth ClientIP UserID; do
      taskset -c "$SIXDB_CPU" "$bin/frame_bounds_parts" "$cols" "$col" \
        | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/parts.jsonl"
    done
    [ -n "${PARTS_ONLY:-}" ] && continue
    taskset -c "$SIXDB_CPU" "$bin/frame_bounds_time" "$cols" 2999808 "${ROUNDS:-7}" $columns \
      | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/time.jsonl"
    taskset -c "$SIXDB_CPU" "$bin/frame_bounds_compose" "$cols" 2999808 "${ROUNDS:-5}" $columns \
      | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/compose.jsonl"
  done
done
