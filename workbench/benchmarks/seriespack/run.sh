#!/bin/bash
set -euo pipefail
# Run from the repository root, locally with SIXDB_RESULTS/SIXDB_CPU or on a worker.
: "${SIXDB_RESULTS:?Choose an output directory, or use workbench/tools/worker.py}"
: "${SIXDB_CPU:?Choose an allowed CPU for sequential benchmark execution}"
profile=${IKEA_PROFILE:-avx2}
case "$profile" in
  avx2) march=x86-64-v3; extra=; tune=zen5 ;;
  avx512) march=x86-64-v4; extra='-mavx512vbmi -mavx512vbmi2 -mgfni'; tune=zen5 ;;
  neon) march=armv8-a+simd; extra=; tune=neoverse-v2 ;;
  *) echo 'IKEA_PROFILE must be avx2, avx512 or neon' >&2; exit 2 ;;
esac
tune=${IKEA_TUNE:-$tune}
compare=${IKEA_COMPARE:-0}
bench_target=ikea_seriespack_bench
if [[ "$compare" == 1 ]]; then bench_target=ikea_seriespack_compare; fi
build="build/benchmarks/seriespack/$profile-$tune"
module_build="$build/ikea"
bench_build="$build/workbench/benchmarks/seriespack"
suite=workbench/benchmarks/seriespack
mkdir -p "$SIXDB_RESULTS"
if [[ -f "$build/build.ninja" ]]; then
  echo 'existing build directory; inspect completed_edges for what rebuilt' > "$SIXDB_RESULTS/build-state.txt"
else
  echo 'new build directory' > "$SIXDB_RESULTS/build-state.txt"
fi
time_format=$'elapsed_seconds=%e\nuser_seconds=%U\nsystem_seconds=%S\nmax_process_rss_kib=%M\nexit_status=%x'
/usr/bin/time -f "$time_format" -o "$SIXDB_RESULTS/configure.resources" \
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_BENCHMARKS=seriespack -DIKEA_COMPARISONS="$compare" -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  -DCMAKE_CXX_FLAGS="$extra"
if [[ -f "$build/.ninja_log" ]]; then cp "$build/.ninja_log" "$SIXDB_RESULTS/build-before.ninja_log"; fi
build_status=0
/usr/bin/time -f "$time_format" -o "$SIXDB_RESULTS/build.resources" \
  cmake --build "$build" --target ikea_seriespack_check ikea_seriespack_ownership_check ikea_examples "$bench_target" \
    -j "${SIXDB_BUILD_JOBS:-1}" || build_status=$?
if [[ -f "$build/.ninja_log" ]]; then cp "$build/.ninja_log" "$SIXDB_RESULTS/build-after.ninja_log"; fi
python3 "$suite/build_evidence.py" "$SIXDB_RESULTS"
if (( build_status )); then exit "$build_status"; fi
python3 ikea/test/seriespack/reference/check.py
if [[ "$compare" == 1 ]]; then
  python3 workbench/spikes/ikea-composition/ikea2-campaign/reference/check.py
fi
python3 ikea/test/headers.py "$build"
taskset -c "$SIXDB_CPU" "$module_build/ikea_seriespack_check" | tee "$SIXDB_RESULTS/checks.txt"
taskset -c "$SIXDB_CPU" "$module_build/ikea_seriespack_ownership_check" | tee "$SIXDB_RESULTS/integration-checks.txt"
for example in ordinary composition pipeline integration; do
  taskset -c "$SIXDB_CPU" "$module_build/ikea_example_seriespack_$example" > "$SIXDB_RESULTS/example-$example.txt"
done
if [[ "${IKEA_COMPILE_DIAGNOSTICS:-0}" == 1 ]]; then
  python3 workbench/tools/compile_probe.py "$build" \
    --source ikea/test/seriespack/mutation/widths_1_8.cpp --source ikea/test/seriespack/mutation/widths_9_16.cpp \
    --source ikea/test/seriespack/mutation/widths_17_24.cpp --output "$SIXDB_RESULTS/compile-probe"
fi
cp "$bench_build/$bench_target" "$module_build/ikea_seriespack_check" "$module_build/ikea_seriespack_ownership_check" \
  "$module_build/libikea_seriespack.a" "$build/compile_commands.json" "$SIXDB_RESULTS/"
if [[ "$compare" == 1 ]]; then cp "$bench_build/predecessor/libikea_seriespack_predecessor.a" "$SIXDB_RESULTS/"; fi
python3 "$suite/build_evidence.py" "$SIXDB_RESULTS" --sizes \
  "$bench_build/$bench_target" "$module_build/ikea_seriespack_check" "$module_build/ikea_seriespack_ownership_check" \
  "$module_build/libikea_seriespack.a"
llvm-objdump-21 -d --demangle "$module_build/libikea_seriespack.a" > "$SIXDB_RESULTS/readers.asm"
if [[ "${IKEA_PIPELINE_CATALOG:-0}" == 1 ]]; then
  cmake --build "$build" --target ikea_seriespack_catalog_inline ikea_seriespack_catalog_cps -j1
  python3 workbench/tools/compile_probe.py "$build" \
    --source "$suite/catalog_inline.cpp" --source "$suite/catalog_cps.cpp" \
    --output "$SIXDB_RESULTS/pipeline-compile"
  cp "$bench_build/libikea_seriespack_catalog_inline.a" "$bench_build/libikea_seriespack_catalog_cps.a" "$SIXDB_RESULTS/"
  python3 "$suite/build_evidence.py" "$SIXDB_RESULTS/pipeline-compile" --sizes \
    "$bench_build/libikea_seriespack_catalog_inline.a" "$bench_build/libikea_seriespack_catalog_cps.a"
fi
taskset -c "$SIXDB_CPU" "$bench_build/$bench_target" \
  --benchmark_min_time="${IKEA_MIN_TIME:-0.1}s" --benchmark_repetitions="${IKEA_REPETITIONS:-5}" \
  --benchmark_enable_random_interleaving=false \
  --benchmark_out_format=json --benchmark_out="$SIXDB_RESULTS/samples.json" "$@"
if [[ "${IKEA_INCREMENTAL:-0}" == 1 ]]; then
  python3 "$suite/incremental.py" "$build" --isa "$profile" \
    --output "$SIXDB_RESULTS/incremental" --jobs "${SIXDB_BUILD_JOBS:-1}"
fi
