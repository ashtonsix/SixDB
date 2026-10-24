#!/bin/bash
# Prototype 1 on a worker: the oracle check, then stage trees against phase
# 2's hand glue over phase 1's real columns, one build per ISA, three process
# repeats.
# Usage: worker.py run workbench/spikes/frame-phase-3/proto/worker.sh --machine zen5|neoverse-v2
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
columns="${COLUMNS:-OS RegionID ResolutionWidth ClientIP UserID EventTime URLHash WatchID CounterID gen_ts gen_walk}"
for isa in $isas; do
  case "$isa" in
    avx2) flags="-DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5" ;;
    avx512) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5" ;;
    *) flags="-DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2" ;;
  esac
  build="build/clang/frame3-$isa"
  out="$SIXDB_RESULTS/$isa"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DSIXDB_SPIKES=frame-phase-1;frame-phase-2;frame-phase-3" $flags > "$out/configure.txt" 2>&1
  /usr/bin/time -v cmake --build "$build" --target frame3_check frame3_measure frame3_writes frame3_calibrate frame3_e2e frame3_seams -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  bin="$build/workbench/spikes/frame-phase-3/proto"
  [ "$isa" = "${isas%% *}" ] && { "$bin/frame3_check" > "$out/check.txt" 2>&1 || true; }
  [ "$isa" = "${isas%% *}" ] && for col in ${E2E_COLUMNS:-OS UserID ClientIP EventTime}; do
    taskset -c "$SIXDB_CPU" "$bin/frame3_e2e" "$cols" "$col" >> "$out/e2e.jsonl" 2>> "$out/e2e.err" || true
  done
  [ -n "${E2E_ONLY:-}" ] && continue
  [ "$isa" = "${isas%% *}" ] && { taskset -c "$SIXDB_CPU" "$bin/frame3_seams" "$cols" 2999808 "${ROUNDS:-7}" > "$out/seams.jsonl" 2> "$out/seams.err" || true; }
  [ -n "${SEAMS_ONLY:-}" ] && continue
  for repeat in 1 2 3; do
    taskset -c "$SIXDB_CPU" "$bin/frame3_measure" "$cols" 2999808 "${ROUNDS:-7}" $columns \
      | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/measure.jsonl"
  done
  [ "$isa" = "${isas%% *}" ] && taskset -c "$SIXDB_CPU" "$bin/frame3_writes" "$cols" 524288 5 ${WRITE_COLUMNS:-OS UserID ClientIP EventTime gen_narrow gen_mixed} \
    > "$out/writes.jsonl" 2> "$out/writes.err" || true
  [ "$isa" = "${isas%% *}" ] && { taskset -c "$SIXDB_CPU" "$bin/frame3_calibrate" "$cols" 2999808 5 ${CALIBRATE_COLUMNS:-OS RegionID ResolutionWidth ClientIP UserID EventTime URLHash CounterID} \
    > "$out/calibrate.jsonl" 2> "$out/calibrate.err" || true; }
done
true
