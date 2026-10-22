#!/bin/bash
# Worker job: the maintained SeriesPack suite built from current sources and
# from current sources with PATCH (a variant patch from this study) applied,
# alternating in two order-reversed trials, plus the maintained SeriesPack
# check built against the patched sources.
set -euo pipefail
study=workbench/spikes/neon-local-transpose
jobs="${JOBS:-$SIXDB_BUILD_JOBS}"
out="$SIXDB_RESULTS"
lscpu --json > "$out/lscpu.json"
clang++-21 --version > "$out/compiler.txt"
candidate="$SIXDB_SOURCE/build/neon-local-transpose/candidate-source"
rm -rf "$candidate"
mkdir -p "$candidate"
tar --exclude=./build -cf - . | tar -xf - -C "$candidate"
patch -p1 --silent -d "$candidate/ikea/include" -i "$PWD/$study/$PATCH"
for variant in current candidate; do
  source=.
  if [[ "$variant" == candidate ]]; then source="$candidate"; fi
  build="$SIXDB_SOURCE/build/neon-local-transpose/suite-$variant"
  cmake -S "$source" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH=armv8-a+simd \
    -DSIXDB_TUNE="${TUNE:-neoverse-v2}" -DSIXDB_TEST_SCOPE=full -DSIXDB_BENCHMARKS=seriespack > "$out/suite-$variant-configure.log"
  cmake --build "$build" --target ikea_seriespack_bench -j "$jobs" > "$out/suite-$variant-build.log" 2>&1
done
build="$SIXDB_SOURCE/build/neon-local-transpose/suite-candidate"
cmake --build "$build" --target ikea_seriespack_check -j "$jobs" > "$out/check-build.log" 2>&1
"$build/ikea/ikea_seriespack_check" > "$out/check.txt" 2>&1
mkdir -p "$out/suite"
for trial in 1 2; do
  order="current candidate"
  if (( trial == 2 )); then order="candidate current"; fi
  for variant in $order; do
    taskset -c "$SIXDB_CPU" "$SIXDB_SOURCE/build/neon-local-transpose/suite-$variant/workbench/benchmarks/seriespack/ikea_seriespack_bench" \
      --suite=all --benchmark_min_time=0.03s --benchmark_repetitions=5 \
      --benchmark_out_format=json --benchmark_out="$out/suite/$variant-$trial.json" \
      > "$out/suite/$variant-$trial.log" 2>&1
  done
done
python3 "$study/compare_suite.py" "$out/suite" --base current --candidate candidate \
  > "$out/suite-summary.txt"
