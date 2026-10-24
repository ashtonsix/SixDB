#!/bin/bash
# SeriesPack remeasure on a worker: one build per ISA, three process repeats.
# Flushed cells use ROWS (default 8M rows); warm cells use one part.
# Usage: worker.py run workbench/spikes/frame-phase-2/seriespack/worker.sh --machine zen5|neoverse-v2
set -euo pipefail
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
  build="build/clang/frame-seriespack-$isa"
  out="$SIXDB_RESULTS/$isa"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release '-DSIXDB_SPIKES=frame-phase-1;frame-phase-2' $flags \
    > "$out/configure.txt" 2>&1
  cmake --build "$build" --target frame_seriespack_measure -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  bin="$build/workbench/spikes/frame-phase-2/seriespack/frame_seriespack_measure"
  for repeat in 1 2 3; do
    taskset -c "$SIXDB_CPU" "$bin" "${ROWS:-8388608}" "${ROUNDS:-11}" \
      | sed "s/^{/{\"repeat\":$repeat,\"isa\":\"$isa\",/" >> "$out/seriespack.jsonl"
  done
done
