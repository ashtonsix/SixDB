#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
out=$SIXDB_RESULTS
build=build/hashtable-overflow-density-zen5
clang++-21 --version > "$out/compiler.txt"
lscpu --json > "$out/lscpu.json"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
  -DSIXDB_SPIKES=hashtable-overflow -DSIXDB_BENCHMARKS= -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 \
  -DOVERFLOW_DENSITY=ON '-DOVERFLOW_CASES=30_10_0;30_10_8;30_10_64;30_20_0;30_20_8' \
  > "$out/configure.log" 2>&1
targets=()
for variant in baseline tag8 filter15; do targets+=("overflow_check_$variant" "overflow_codegen_$variant"); done
for case in 30_10_0 30_10_8 30_10_64 30_20_0 30_20_8; do
  for variant in baseline tag8 tag8_c30 tag8_c31 filter15 filter15_c30 filter15_c31 peers; do
    targets+=("overflow_${variant}_$case")
  done
done
cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$out/build.log" 2>&1
cp "$build/compile_commands.json" "$out/compile.json"
mkdir -p "$out/binaries" "$out/codegen"
for variant in baseline tag8 filter15; do
  timeout 30 "$build/workbench/spikes/hashtable-overflow/overflow_check_$variant" > "$out/check-$variant.txt"
  cp "$build/workbench/spikes/hashtable-overflow/$variant/include/delta.json" "$out/delta-$variant.json"
done
python3 workbench/spikes/hashtable-overflow/codegen.py "$build" "$out/codegen"
for target in "${targets[@]}"; do
  [[ $target != overflow_check_* && $target != overflow_codegen_* ]] || continue
  cp "$build/workbench/spikes/hashtable-overflow/$target" "$out/binaries/"
done
python3 workbench/spikes/hashtable-overflow/run.py "$build" "$out/samples" --cpu "$SIXDB_CPU" \
  --density --loads 890,925,980 --cases 30_10_0,30_10_8,30_10_64,30_20_0,30_20_8 > "$out/progress.txt" 2>&1
python3 workbench/spikes/hashtable-overflow/audit.py "$out"
python3 workbench/spikes/hashtable-overflow/summarize.py "$out" --output "$out/summary"
printf 'complete\n' > "$out/complete.txt"
