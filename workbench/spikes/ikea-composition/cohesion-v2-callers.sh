#!/bin/bash
# Compile maintained research/benchmark adapters and execute their owner checks.
# Timings are deliberately left to the paired native runner.
set -euo pipefail
case "$(uname -m)" in
  aarch64) profile=neon; march=armv8-a+simd; tune=neoverse-v2 ;;
  *) profile=avx2; march=x86-64-v3; tune=zen5 ;;
esac
dir="build/ikea/interface-callers/$profile"
spikes='streampack;packed-filters;tuple-layout;ikea-composition;intervals'
if test "$profile" = neon; then spikes="$spikes;neon-local-transpose;bec256-composition"; fi
cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES="$spikes" \
  '-DSIXDB_BENCHMARKS=seriespack;tuplepack;bec;hashtable' -DIKEA_COMPARISONS=ON \
  -DSIXDB_HEAVY_COMPILE_JOBS=4 > "$SIXDB_RESULTS/configure.log" 2>&1
targets=(ikea_seriespack_bench ikea_seriespack_compare ikea_tuplepack_bench
  ikea_tuplepack_word_bench ikea_bec_bench ikea_hashtable_reads
  ikea_hashtable_operations_k14_b10_v8 ikea_hashtable_operations_k30_b10_v64
  ikea_hashtable_accumulation_basic_8 ikea_hashtable_accumulation_wide_64
  streampack_implementation_bench streampack_implementation_loom_check streampack_implementation_orbital_check
  streampack_model_check streampack_publication_check streampack_widths_publication_check
  streampack_rewrite_check packed_filters_early_guards packed_filters_bench
  tuple_packet_crossover tuple_gpr_probe tuple_group_probe seriespack_placement_probe
  intervals_consumer_seams intervals_consumer_edits intervals_directory_reads)
if test "$profile" = neon; then
  targets+=(neon_local_transpose_preset_bench_current neon_local_transpose_check_current
    bec_composition_check bec_casing_check bec_algebra_check bec_composition_bench)
fi
failed=0
cmake --build "$dir" --target "${targets[@]}" -j "$SIXDB_BUILD_JOBS" -- -k 0 \
  > "$SIXDB_RESULTS/build.log" 2>&1 || failed=1
cp "$dir/compile_commands.json" "$SIXDB_RESULTS/compile_commands.json"
cp "$dir/CMakeCache.txt" "$SIXDB_RESULTS/CMakeCache.txt"
# These retained tests perform real mutation/publication and independent value
# checks. No benchmark time is interpreted as a performance qualification here.
taskset -c "$SIXDB_CPU" ctest --test-dir "$dir" --output-on-failure --no-tests=error \
  -R '^(streampack_implementation_(loom|orbital)_check|streampack_publication_check|streampack_widths_publication_check|streampack_rewrite_check)$' \
  > "$SIXDB_RESULTS/checks.log" 2>&1 || failed=1
for program in $(test "$profile" != neon || echo bec_composition_check bec_casing_check bec_algebra_check); do
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/bec256-composition/$program" \
    > "$SIXDB_RESULTS/$program.log" 2>&1 || failed=1
done
exit "$failed"
