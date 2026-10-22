#!/bin/bash
# Worker job for the graduated implementation: Ikea validation under both ARM
# tunes, ASan/UBSan SeriesPack checks, the spike's alternating variants, and
# the maintained SeriesPack suite built from current and `original` sources.
set -euo pipefail
study=workbench/spikes/neon-local-transpose
jobs="${JOBS:-$SIXDB_BUILD_JOBS}"
out="$SIXDB_RESULTS"
progress() { echo "{\"phase\":\"$1\",\"time\":$(date +%s)}" >> "$out/progress.jsonl"; }
lscpu --json > "$out/lscpu.json"
clang++-21 --version > "$out/compiler.txt"

progress validate
for tune in neoverse-v2 neoverse-v3; do
  build="build/neon-local-transpose/validate-$tune"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH=armv8-a+simd \
    -DSIXDB_TUNE="$tune" > "$out/validate-$tune-configure.log"
  cmake --build "$build" --target ikea_validate -j "$jobs" > "$out/validate-$tune.log" 2>&1
done

progress sanitizers
build=build/neon-local-transpose/asan
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIXDB_MARCH=armv8-a+simd \
  -DSIXDB_TUNE=neoverse-v2 '-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer' \
  > "$out/asan-configure.log"
cmake --build "$build" --target ikea_seriespack_check ikea_seriespack_descriptor_check \
  -j "$jobs" > "$out/asan-build.log" 2>&1
"$build/ikea/ikea_seriespack_check" > "$out/asan-checks.txt" 2>&1
"$build/ikea/ikea_seriespack_descriptor_check" >> "$out/asan-checks.txt" 2>&1

progress spike
mkdir -p "$out/spike"
SIXDB_RESULTS="$out/spike" bash "$study/cloud.sh" "$@"

progress maintained-suite
original="$SIXDB_SOURCE/build/neon-local-transpose/original-source"
rm -rf "$original"
mkdir -p "$original"
tar --exclude=./build -cf - . | tar -xf - -C "$original"
patch -p1 --silent -d "$original/ikea/include" -i "$PWD/$study/original.patch"
for variant in current original; do
  source=.
  if [[ "$variant" == original ]]; then source="$original"; fi
  build="$SIXDB_SOURCE/build/neon-local-transpose/suite-$variant"
  cmake -S "$source" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH=armv8-a+simd \
    -DSIXDB_TUNE=neoverse-v2 -DSIXDB_BENCHMARKS=seriespack > "$out/suite-$variant-configure.log"
  cmake --build "$build" --target ikea_seriespack_bench -j "$jobs" > "$out/suite-$variant-build.log" 2>&1
done
mkdir -p "$out/suite"
for trial in 1 2; do
  order="original current"
  if (( trial == 2 )); then order="current original"; fi
  for variant in $order; do
    taskset -c "$SIXDB_CPU" "$SIXDB_SOURCE/build/neon-local-transpose/suite-$variant/workbench/benchmarks/seriespack/ikea_seriespack_bench" \
      --suite=all --benchmark_min_time=0.03s --benchmark_repetitions=5 \
      --benchmark_out_format=json --benchmark_out="$out/suite/$variant-$trial.json" \
      > "$out/suite/$variant-$trial.log" 2>&1
  done
done
python3 "$study/compare_suite.py" "$out/suite" > "$out/suite-summary.txt"
progress complete
