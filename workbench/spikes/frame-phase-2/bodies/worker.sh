#!/bin/bash
# Front C leaf sweep on a worker: one build per ISA, the oracle check, then
# packed widths and runs cells at three tiers (one part, LLC-sized, DRAM).
# Then phase 1's real columns through the leaves (columns.cpp), one part and
# the full 3M-row slice, under the bytes and packed-only policies.
# Usage: worker.py run workbench/spikes/frame-phase-2/bodies/worker.sh --machine zen5|neoverse-v2
#   env: ISAS, ROUNDS, TIERS ("l2 llc dram"), WIDTHS_L2, WIDTHS_BIG, RUN_W, RUN_N,
#        SWEEP=0 skips the synthetic sweep, COLUMNS=0 the real columns,
#        EXTRA=0 the patched, compaction and histogram cells, PEERS=0 the
#        TurboPFor/FastLanes arms
set -euo pipefail
inputs=workbench/spikes/frame-phase-1/encoding/worker-inputs.json
if [ "${COLUMNS:-1}" = 1 ]; then
  cp "$inputs" "$SIXDB_RESULTS/input-artifact.json"
  python3 workbench/tools/artifacts.py fetch "$SIXDB_RESULTS/input-artifact.json" "$SIXDB_RESULTS/in" \
    > "$SIXDB_RESULTS/input-fetch.txt" 2>&1
fi
cols="$SIXDB_RESULTS/in/inputs/cols"
columns="OS:RegionID RegionID:OS ResolutionWidth:OS EventTime:EventTime UserID:OS URLHash:UserID WatchID:EventTime ClientIP:RegionID ConnectTiming:OS IsRefresh CounterID"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
grep -m1 -E '^(flags|Features)' /proc/cpuinfo > "$SIXDB_RESULTS/cpu-flags.txt" || true
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
case "$(uname -m)" in
  x86_64) isas="${ISAS:-avx512 avx2}" ;;
  *) isas="${ISAS:-native}" ;;
esac
rounds="${ROUNDS:-9}"
widths_l2="${WIDTHS_L2:-$(seq -s ' ' 1 32)}"
widths_big="${WIDTHS_BIG:-1 2 3 4 6 8 9 12 16 20 24 32}"
run_w="${RUN_W:-8 16}"
run_n="${RUN_N:-2 4 8 16 24 32 48 64 128}"
declare -A rows=([l2]=65536 [llc]=4194304 [dram]=134217728)
for isa in $isas; do
  case "$isa" in
    avx2) flags="-DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5" ;;
    avx512) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5" ;;
    *) flags="-DSIXDB_TUNE=neoverse-v2" ;;
  esac
  build="build/clang/frame-bodies-$isa"
  out="$SIXDB_RESULTS/$isa"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release '-DSIXDB_SPIKES=frame-phase-1;frame-phase-2' $flags \
    -DSIXDB_FRAME_CODEC_CONTROLS="${PEERS:-1}" \
    > "$out/configure.txt" 2>&1
  targets="frame_bodies_check frame_bodies_measure frame_bodies_columns"
  [ "${PEERS:-1}" = 1 ] && targets="$targets frame_bodies_peers"
  cmake --build "$build" --target $targets -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  dir="$build/workbench/spikes/frame-phase-2/bodies"
  "$dir/frame_bodies_check" > "$out/check.txt" 2>&1 || { echo "check failed: $isa" >> "$SIXDB_RESULTS/failed.txt"; continue; }
  if [ "${COLUMNS:-1}" = 1 ]; then
    for part_rows in 65536 2999808; do
      for policy in bytes bytes+starts packed; do
        taskset -c "$SIXDB_CPU" "$dir/frame_bodies_columns" "$cols" "$part_rows" "$rounds" "$policy" $columns \
          >> "$out/columns.jsonl"
      done
    done
  fi
  if [ "${PEERS:-1}" = 1 ]; then
    for tier in ${TIERS:-l2 llc dram}; do
      if [ "$tier" = l2 ]; then widths="$widths_l2"; else widths="$widths_big"; fi
      taskset -c "$SIXDB_CPU" "$dir/frame_bodies_peers" "${rows[$tier]}" "$rounds" $widths \
        | sed "s/^{/{\"tier\":\"$tier\",/" >> "$out/peers.jsonl"
    done
  fi
  [ "${SWEEP:-1}" = 1 ] || continue
  for tier in ${TIERS:-l2 llc dram}; do
    if [ "$tier" = l2 ]; then widths="$widths_l2"; else widths="$widths_big"; fi
    taskset -c "$SIXDB_CPU" "$dir/frame_bodies_measure" "${rows[$tier]}" "$rounds" packed $widths \
      | sed "s/^{/{\"tier\":\"$tier\",/" >> "$out/samples.jsonl"
    for w in $run_w; do
      taskset -c "$SIXDB_CPU" "$dir/frame_bodies_measure" "${rows[$tier]}" "$rounds" runs "$w" $run_n \
        | sed "s/^{/{\"tier\":\"$tier\",/" >> "$out/samples.jsonl"
    done
    [ "$tier" = llc ] || [ "${EXTRA:-1}" = 0 ] && continue
    for e in 1 4 16; do
      taskset -c "$SIXDB_CPU" "$dir/frame_bodies_measure" "${rows[$tier]}" "$rounds" patched "$e" 4 8 12 16 20 \
        | sed "s/^{/{\"tier\":\"$tier\",/" >> "$out/samples.jsonl"
    done
    taskset -c "$SIXDB_CPU" "$dir/frame_bodies_measure" "${rows[$tier]}" "$rounds" compact 1 3 5 8 12 16 20 32 \
      | sed "s/^{/{\"tier\":\"$tier\",/" >> "$out/samples.jsonl"
    taskset -c "$SIXDB_CPU" "$dir/frame_bodies_measure" "${rows[$tier]}" "$rounds" histogram 1 2 3 4 5 6 7 8 \
      | sed "s/^{/{\"tier\":\"$tier\",/" >> "$out/samples.jsonl"
  done
done
echo done > "$SIXDB_RESULTS/status.txt"
