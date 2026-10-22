#!/bin/bash
# Worker job: build the variants, run the independent transpose check and
# the maintained SeriesPack check against each overlay, then time alternating
# variants and record code size. VARIANTS selects the variants (the first is the
# speedup baseline) and BINARY the benchmark (bench, or preset_bench without
# code sizes); arguments pass to run.py (for example --rounds).
set -euo pipefail
study=workbench/spikes/neon-local-transpose
build="$SIXDB_SOURCE/build/neon-local-transpose"
out="$build/workbench/spikes/neon-local-transpose"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH=armv8-a+simd \
  -DSIXDB_TUNE="${TUNE:-neoverse-v2}" -DSIXDB_SPIKES=neon-local-transpose > "$SIXDB_RESULTS/configure.log"
targets=""
variants="${VARIANTS:-original bit4 current}"
binary="${BINARY:-bench}"
for v in $variants; do
  targets="$targets neon_local_transpose_${binary}_$v neon_local_transpose_check_$v neon_local_transpose_seriespack_check_$v"
done
cmake --build "$build" --target $targets -j "${JOBS:-$SIXDB_BUILD_JOBS}" > "$SIXDB_RESULTS/build.log" 2>&1
for v in $variants; do
  "$out/neon_local_transpose_check_$v" >> "$SIXDB_RESULTS/checks.txt"
  "$out/neon_local_transpose_seriespack_check_$v" > "$SIXDB_RESULTS/seriespack-check-$v.txt" 2>&1
  echo "$v: maintained SeriesPack check passed" >> "$SIXDB_RESULTS/checks.txt"
done
[[ "$binary" != bench ]] || python3 "$study/sizes.py" "$out" --variants "${variants// /,}" --output "$SIXDB_RESULTS/sizes.json" > "$SIXDB_RESULTS/sizes.txt"
python3 "$study/run.py" "$out" --variants "${variants// /,}" --binary "$binary" --cpu "$SIXDB_CPU" \
  --output "$SIXDB_RESULTS/samples.json" "$@" > "$SIXDB_RESULTS/summary.txt"
