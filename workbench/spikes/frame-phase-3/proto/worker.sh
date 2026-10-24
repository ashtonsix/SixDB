#!/bin/bash
# Prototype 1 on a worker: the oracle check, then stage trees against phase
# 2's hand glue over phase 1's real columns, one build per ISA, three process
# repeats.
# Usage: worker.py run workbench/spikes/frame-phase-3/proto/worker.sh --machine zen5|neoverse-v2
set -euo pipefail
inputs=workbench/spikes/frame-phase-1/encoding/worker-inputs.json
# Inputs come from Workbench's cached-input helper (digest-keyed in the
# worker's data cache), never under SIXDB_RESULTS; only the reference is kept.
cp "$inputs" "$SIXDB_RESULTS/input-artifact.json"
input_root=$(python3 workbench/tools/artifacts.py input "$inputs" 2> "$SIXDB_RESULTS/input-fetch.txt")
cols="$input_root/inputs/cols"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
case "$(uname -m)" in
  x86_64) isas="${ISAS:-avx512 avx2}" ;;
  *) isas="${ISAS:-native}" ;;
esac
columns="${COLUMNS:-OS RegionID ResolutionWidth ClientIP UserID EventTime URLHash WatchID CounterID gen_ts gen_walk}"
for leaf in ${LEAVES:-runtime}; do
for isa in $isas; do
  case "$isa" in
    avx2) flags="-DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5" ;;
    avx512) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5" ;;
    *) flags="-DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2" ;;
  esac
  build="build/clang/frame3-$isa-$leaf"
  out="$SIXDB_RESULTS/$isa-$leaf"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DSIXDB_SPIKES=frame-phase-1;frame-phase-2;frame-phase-3" $flags -DFRAME3_LEAF=$leaf > "$out/configure.txt" 2>&1
  /usr/bin/time -v cmake --build "$build" --target frame3_check frame3_measure frame3_writes frame3_calibrate frame3_e2e frame3_seams -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  bin="$build/workbench/spikes/frame-phase-3/proto"
  # Ikea LEAD's owner and control checks share this build (OWNER_TARGETS=1).
  if [ -n "${OWNER_TARGETS:-}" ] && [ "$isa" = "${isas%% *}" ]; then
    cmake --build "$build" --target frame3_owner_validate frame3_native_controls_check -j "$SIXDB_BUILD_JOBS" > "$out/owner-build.txt" 2>&1 || true
    ctest --test-dir "$build" -R "frame3_owner|frame3_native_controls" --output-on-failure > "$out/owner-ctest.txt" 2>&1 || true
    for t in frame3_owner_validate frame3_native_controls_check; do
      f=$(find "$build/workbench/spikes/frame-phase-3" -type f -name "$t" -perm -u+x | head -1)
      [ -n "$f" ] && { "$f" > "$out/$t.txt" 2>&1; echo "exit $?" >> "$out/$t.txt"; }
    done
  fi
  [ "$isa" = "${isas%% *}" ] && { "$bin/frame3_check" > "$out/check.txt" 2>&1 || true; }
  [ "$isa" = "${isas%% *}" ] && for col in ${E2E_COLUMNS:-OS UserID ClientIP EventTime}; do
    taskset -c "$SIXDB_CPU" "$bin/frame3_e2e" "$cols" "$col" >> "$out/e2e.jsonl" 2>> "$out/e2e.err" || true
  done
  [ -n "${E2E_ONLY:-}" ] && continue
  [ "$isa" = "${isas%% *}" ] && { taskset -c "$SIXDB_CPU" "$bin/frame3_seams" "$cols" 2999808 "${ROUNDS:-7}" > "$out/seams.jsonl" 2> "$out/seams.err" || true; }
  [ -n "${SEAMS_ONLY:-}" ] && continue
  for repeat in 1 2 3; do
    taskset -c "$SIXDB_CPU" "$bin/frame3_measure" "$cols" 2999808 "${ROUNDS:-7}" $columns \
      | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa-$leaf\",/" >> "$out/measure.jsonl"
  done
  [ "$isa" = "${isas%% *}" ] && taskset -c "$SIXDB_CPU" "$bin/frame3_writes" "$cols" 524288 5 ${WRITE_COLUMNS:-OS UserID ClientIP EventTime gen_narrow gen_mixed} \
    > "$out/writes.jsonl" 2> "$out/writes.err" || true
  [ "$isa" = "${isas%% *}" ] && { taskset -c "$SIXDB_CPU" "$bin/frame3_calibrate" "$cols" 2999808 5 ${CALIBRATE_COLUMNS:-OS RegionID ResolutionWidth ClientIP UserID EventTime URLHash CounterID} \
    > "$out/calibrate.jsonl" 2> "$out/calibrate.err" || true; }
done
done
true
