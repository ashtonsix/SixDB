#!/bin/bash
# Worker job: the maintained TuplePack benchmark built from current sources and
# with PATCH applied, alternating in two order-reversed trials (FILTER selects
# cases), plus every TuplePack check and the filter check on the patched source.
set -euo pipefail
here=workbench/spikes/neon-tuplepack-masked
jobs="${JOBS:-$SIXDB_BUILD_JOBS}"
out="$SIXDB_RESULTS"
lscpu --json > "$out/lscpu.json"
clang++-21 --version > "$out/compiler.txt"
candidate="$SIXDB_SOURCE/build/neon-tuplepack-masked/candidate-source"
rm -rf "$candidate"
mkdir -p "$candidate"
tar --exclude=./build -cf - . | tar -xf - -C "$candidate"
patch -p1 --silent -d "$candidate/ikea/include" -i "$PWD/$here/${PATCH:-rowgather.patch}"
checks="ikea_tuplepack_wire_check ikea_tuplepack_transfer_check ikea_tuplepack_operations_check
  ikea_tuplepack_execution_check ikea_tuplepack_packets_check ikea_tuplepack_gpr_check
  ikea_tuplepack_ownership_check ikea_filters_check"
for variant in current candidate; do
  source=.
  if [[ "$variant" == candidate ]]; then source="$candidate"; fi
  build="$SIXDB_SOURCE/build/neon-tuplepack-masked/$variant"
  cmake -S "$source" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH=armv8-a+simd \
    -DSIXDB_TUNE="${TUNE:-neoverse-v2}" -DSIXDB_TEST_SCOPE=full -DSIXDB_BENCHMARKS=tuplepack \
    > "$out/$variant-configure.log"
  cmake --build "$build" --target ikea_tuplepack_bench -j "$jobs" > "$out/$variant-build.log" 2>&1
done
build="$SIXDB_SOURCE/build/neon-tuplepack-masked/candidate"
cmake --build "$build" --target $checks -j "$jobs" > "$out/check-build.log" 2>&1
for check in $checks; do
  "$build/ikea/$check" > "$out/$check.txt" 2>&1
  echo "$check passed" >> "$out/checks.txt"
done
mkdir -p "$out/suite"
for trial in 1 2; do
  order="current candidate"
  if (( trial == 2 )); then order="candidate current"; fi
  for variant in $order; do
    taskset -c "$SIXDB_CPU" "$SIXDB_SOURCE/build/neon-tuplepack-masked/$variant/workbench/benchmarks/tuplepack/ikea_tuplepack_bench" \
      --benchmark_filter="${FILTER:-.}" --benchmark_min_time=0.05s --benchmark_repetitions=5 \
      --benchmark_out_format=json --benchmark_out="$out/suite/$variant-$trial.json" \
      > "$out/suite/$variant-$trial.log" 2>&1
  done
done
python3 workbench/spikes/neon-local-transpose/compare_suite.py "$out/suite" --base current \
  --candidate candidate > "$out/summary.txt"
