#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) profiles=neon; tune=neoverse-v2 ;;
  *) profiles="avx2 avx512"; tune="${FILTER_TUNE:-zen5}" ;;
esac
for profile in $profiles; do
  case "$profile" in
    neon) march=armv8-a+simd ;;
    avx2) march=x86-64-v3 ;;
    avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
  esac
  dir="build/filter-early/$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=packed-filters
  cmake --build "$dir" --target packed_filters_early packed_filters_early_guards -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/packed-filters/packed_filters_early_guards" \
    > "$SIXDB_RESULTS/$profile-guards.txt" 2>&1
  exe="$dir/workbench/spikes/packed-filters/packed_filters_early"
  output="$SIXDB_RESULTS/$profile-focus.csv"
  first=1
  for spec in 'head8 12 0' 'head8 64 0' 'head8 64 1' 'local 3 0' 'local 64 0' \
              'tuple_pair 8 0' 'tuple_pair_stride4 8 0'; do
    read -r module width op <<< "$spec"
    for pattern in uniform reject_first shared_prefix mixed50 all_equal; do
      taskset -c "$SIXDB_CPU" "$exe" --module "$module" --width "$width" --op "$op" \
        --pattern "$pattern" --iterations 500000 --repeats 7 \
        > "$SIXDB_RESULTS/temp.csv" 2>> "$SIXDB_RESULTS/$profile-check.txt"
      if [ "$first" = 1 ]; then cp "$SIXDB_RESULTS/temp.csv" "$output"; first=0;
      else tail -n +2 "$SIXDB_RESULTS/temp.csv" >> "$output"; fi
    done
  done
  rm "$SIXDB_RESULTS/temp.csv"
  python3 workbench/spikes/packed-filters/early-report.py "$output" "$SIXDB_RESULTS/$profile-focus-summary.csv"
  if [ ! -f "$SIXDB_RESULTS/$profile-disassembly.txt" ]; then
    llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/$profile-disassembly.txt"
  fi
  if [ "$profile" = avx512 ]; then
    cmake --build "$dir" --target packed_filters_early_wide -j "$SIXDB_BUILD_JOBS"
    wide="$dir/workbench/spikes/packed-filters/packed_filters_early_wide"
    taskset -c "$SIXDB_CPU" "$wide" > "$SIXDB_RESULTS/zmm.csv" 2> "$SIXDB_RESULTS/zmm-check.txt"
    python3 workbench/spikes/packed-filters/early-report.py "$SIXDB_RESULTS/zmm.csv" "$SIXDB_RESULTS/zmm-summary.csv"
    llvm-objdump-21 -d --demangle "$wide" > "$SIXDB_RESULTS/zmm-disassembly.txt"
  fi
  # Whole-process counters are a diagnostic, not timed-loop-only counts.
  # Runtime dominates setup at this iteration count. Keep unavailable receipts.
  if command -v perf >/dev/null 2>&1; then
    for variant in progressive early; do
      perf stat -x, -e cycles,instructions,branches,branch-misses -o "$SIXDB_RESULTS/$profile-perf-$variant.txt" \
        taskset -c "$SIXDB_CPU" "$exe" --module head8 --width 64 --op 0 --pattern mixed50 \
        --variant "$variant" --iterations 3000000 --repeats 3 \
        > "$SIXDB_RESULTS/$profile-perf-$variant.csv" 2> "$SIXDB_RESULTS/$profile-perf-$variant.log" || true
    done
  else
    printf 'perf unavailable on worker\n' > "$SIXDB_RESULTS/$profile-perf-unavailable.txt"
  fi
done
