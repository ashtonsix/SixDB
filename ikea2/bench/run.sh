#!/bin/bash
set -euo pipefail
case "${IKEA2_PROFILE:-avx2}" in
  avx2) march=x86-64-v3; extra=; tune=zen5 ;;
  avx512) march=x86-64-v4; extra='-mavx512vbmi -mavx512vbmi2 -mgfni'; tune=zen5 ;;
  neon) march=armv8-a+simd; extra=; tune=neoverse-v2 ;;
  *) exit 2 ;;
esac
tune=${IKEA2_TUNE:-$tune}
compare=${IKEA2_COMPARE:-1}
bench_target=ikea2_bench
if [[ "$compare" == 1 ]]; then bench_target=ikea2_compare; fi
build="build/ikea2/${IKEA2_PROFILE:-avx2}-$tune"
if [[ -f "$build/build.ninja" ]]; then
  echo 'existing build directory; inspect completed_edges for what rebuilt' > "$SIXDB_RESULTS/build-state.txt"
else
  echo 'new build directory' > "$SIXDB_RESULTS/build-state.txt"
fi
time_format=$'elapsed_seconds=%e\nuser_seconds=%U\nsystem_seconds=%S\nmax_process_rss_kib=%M\nexit_status=%x'
/usr/bin/time -f "$time_format" -o "$SIXDB_RESULTS/configure.resources" \
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DIKEA2_BENCHMARKS=ON -DIKEA2_COMPARISONS="$compare" -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  -DCMAKE_CXX_FLAGS="$extra"
if [[ -f "$build/.ninja_log" ]]; then cp "$build/.ninja_log" "$SIXDB_RESULTS/build-before.ninja_log"; fi
build_status=0
/usr/bin/time -f "$time_format" -o "$SIXDB_RESULTS/build.resources" \
  cmake --build "$build" --target ikea2_check ikea2_integration_check ikea2_examples "$bench_target" \
    -j "${SIXDB_BUILD_JOBS:-1}" || build_status=$?
if [[ -f "$build/.ninja_log" ]]; then cp "$build/.ninja_log" "$SIXDB_RESULTS/build-after.ninja_log"; fi
python3 ikea2/bench/build_evidence.py "$SIXDB_RESULTS"
if (( build_status )); then exit "$build_status"; fi
python3 ikea2/test/reference/check.py
python3 ikea2/test/headers.py "$build"
taskset -c "$SIXDB_CPU" "$build/ikea2/ikea2_check" | tee "$SIXDB_RESULTS/checks.txt"
taskset -c "$SIXDB_CPU" "$build/ikea2/ikea2_integration_check" | tee "$SIXDB_RESULTS/integration-checks.txt"
taskset -c "$SIXDB_CPU" "$build/ikea2/ikea2_ordinary_check" > "$SIXDB_RESULTS/ordinary-checks.txt"
for example in composition pipeline integration; do
  taskset -c "$SIXDB_CPU" "$build/ikea2/ikea2_example_$example" > "$SIXDB_RESULTS/example-$example.txt"
done
if [[ "${IKEA2_COMPILE_DIAGNOSTICS:-0}" == 1 ]]; then
  python3 workbench/tools/compile_probe.py "$build" \
    --source ikea2/test/mutation/widths_1_8.cpp --source ikea2/test/mutation/widths_9_16.cpp \
    --source ikea2/test/mutation/widths_17_24.cpp --output "$SIXDB_RESULTS/compile-probe"
fi
cp "$build/ikea2/$bench_target" "$build/ikea2/ikea2_check" "$build/ikea2/ikea2_integration_check" \
  "$build/ikea2/libikea2_seriespack.a" \
  "$build/compile_commands.json" "$SIXDB_RESULTS/"
if [[ "$compare" == 1 ]]; then cp "$build/ikea2/predecessor/libikea2_predecessor.a" "$SIXDB_RESULTS/"; fi
llvm-size-21 -A "$build/ikea2/libikea2_seriespack.a" > "$SIXDB_RESULTS/sections.txt"
python3 ikea2/bench/build_evidence.py "$SIXDB_RESULTS" --sizes \
  "$build/ikea2/$bench_target" "$build/ikea2/ikea2_check" "$build/ikea2/ikea2_integration_check" \
  "$build/ikea2/libikea2_seriespack.a"
llvm-objdump-21 -d --demangle "$build/ikea2/libikea2_seriespack.a" > "$SIXDB_RESULTS/readers.asm"

if [[ "${IKEA2_PIPELINE_CATALOG:-0}" == 1 ]]; then
  cmake --build "$build" --target ikea2_catalog_inline ikea2_catalog_cps -j 1
  python3 workbench/tools/compile_probe.py "$build" \
    --source ikea2/bench/catalog_inline.cpp --source ikea2/bench/catalog_cps.cpp \
    --output "$SIXDB_RESULTS/pipeline-compile"
  cp "$build/ikea2/libikea2_catalog_inline.a" "$build/ikea2/libikea2_catalog_cps.a" "$SIXDB_RESULTS/"
  python3 ikea2/bench/build_evidence.py "$SIXDB_RESULTS/pipeline-compile" --sizes \
    "$build/ikea2/libikea2_catalog_inline.a" "$build/ikea2/libikea2_catalog_cps.a"
  llvm-size-21 -A "$build/ikea2/libikea2_catalog_inline.a" "$build/ikea2/libikea2_catalog_cps.a" > "$SIXDB_RESULTS/pipeline-sections.txt"
fi

taskset -c "$SIXDB_CPU" "$build/ikea2/$bench_target" \
  --benchmark_min_time=0.03s --benchmark_repetitions=3 \
  --benchmark_out_format=json --benchmark_out="$SIXDB_RESULTS/samples.json" "$@"

if [[ "${IKEA2_INCREMENTAL:-0}" == 1 ]]; then
  case "${IKEA2_PROFILE:-avx2}" in avx512) native_isa=avx512;; avx2) native_isa=avx2;; neon) native_isa=neon;; esac
  python3 ikea2/bench/incremental.py "$build" --isa "$native_isa" \
    --output "$SIXDB_RESULTS/incremental" --jobs "${SIXDB_BUILD_JOBS:-1}"
fi
