#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Use worker.py or choose output}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
profile="${1:-avx512}"
native=1
case "$profile" in
  avx512) march=znver5; tune=zen5;;
  avx2) march=x86-64-v3; tune=zen5; native=0;;
  granite) march=graniterapids; tune=granite-rapids;;
  neon) march=armv8.2-a+simd; tune=neoverse-v2;;
  *) exit 2;;
esac
build="build/bec256/$profile"
mkdir -p "$SIXDB_RESULTS"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=bec256-composition -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
targets=(ikea_bec256_check ikea_bec256_composition_check
  ikea_example_bec256_ordinary ikea_example_bec256_native ikea_example_bec256_integration)
if ((native)); then targets+=(bec_composition_check bec_casing_check bec_algebra_check bec_composition_bench); fi
/usr/bin/time -v -o "$SIXDB_RESULTS/build.resources" cmake --build "$build" \
  --target "${targets[@]}" \
  -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
"$build/ikea/ikea_bec256_check" > "$SIXDB_RESULTS/checks.log"
"$build/ikea/ikea_bec256_composition_check" >> "$SIXDB_RESULTS/checks.log"
"$build/ikea/ikea_example_bec256_ordinary" >> "$SIXDB_RESULTS/checks.log"
"$build/ikea/ikea_example_bec256_native" >> "$SIXDB_RESULTS/checks.log"
"$build/ikea/ikea_example_bec256_integration" >> "$SIXDB_RESULTS/checks.log"
python3 ikea/test/headers.py "$build" --module bec256 >> "$SIXDB_RESULTS/checks.log"
if ((!native)); then exit 0; fi
"$build/workbench/spikes/bec256-composition/bec_composition_check" >> "$SIXDB_RESULTS/checks.log"
"$build/workbench/spikes/bec256-composition/bec_casing_check" >> "$SIXDB_RESULTS/checks.log"
"$build/workbench/spikes/bec256-composition/bec_algebra_check" >> "$SIXDB_RESULTS/checks.log"
export BEC_WINDOWS
BEC_WINDOWS=$(python3 workbench/tools/datasets.py fetch workbench/spikes/bec-packed-metadata/windows.json)
cp "$build/workbench/spikes/bec256-composition/bec_composition_bench" "$SIXDB_RESULTS/"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/"
llvm-size-21 "$SIXDB_RESULTS/bec_composition_bench" "$build/ikea/libikea_bec256.a" > "$SIXDB_RESULTS/size.txt"
llvm-nm-21 -S --size-sort --demangle "$build/workbench/spikes/bec256-composition/libbec_composition.a" > "$SIXDB_RESULTS/symbols.txt"
taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/bec_composition_bench" \
  --benchmark_min_time="${BEC_MIN_TIME:-0.003}s" --benchmark_repetitions="${BEC_REPETITIONS:-3}" \
  --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
  --benchmark_out="$SIXDB_RESULTS/samples.json" \
  --benchmark_filter="${BEC_FILTER:-.}" > "$SIXDB_RESULTS/timings.log" 2>&1
if [[ ${BEC_ROUTINE:-0} == 1 ]]; then
  SIXDB_RESULTS="$SIXDB_RESULTS/routine" bash workbench/benchmarks/bec256/run.sh "$profile"
fi
