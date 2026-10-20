#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) profiles=neon; tune=neoverse-v2 ;;
  *) profiles="avx2 avx512"; tune="${BOUNDLIST_TUNE:-zen5}" ;;
esac
for profile in $profiles; do
  case "$profile" in
    neon) march=armv8-a+simd ;;
    avx2) march=x86-64-v3 ;;
    avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
  esac
  dir="build/boundlist/$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=boundlist
  cmake --build "$dir" --target boundlist_check boundlist_search boundlist_codegen -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_check" > "$SIXDB_RESULTS/$profile-check.txt"
  exe="$dir/workbench/spikes/boundlist/boundlist_search"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" > "$SIXDB_RESULTS/$profile-search-assembly.txt"
  python3 workbench/spikes/boundlist/search-audit.py "$SIXDB_RESULTS/$profile-search-assembly.txt" > "$SIXDB_RESULTS/$profile-search-audit.json"
  llvm-objdump-21 -d --no-show-raw-insn "$dir/workbench/spikes/boundlist/CMakeFiles/boundlist_codegen.dir/codegen.cpp.o" \
    > "$SIXDB_RESULTS/$profile-kernels.txt"
  for bits in 16 32 64; do
    for count in ${BOUNDLIST_SEARCH_COUNTS:-64 256 1024 4096 16384}; do
      case "$count" in 256) ops=262144; reps=7 ;; *) ops=32768; reps=5 ;; esac
      for distribution in 0 1 2; do
        taskset -c "$SIXDB_CPU" "$exe" "$bits" "$count" "$distribution" "$ops" "$reps" \
          > "$SIXDB_RESULTS/$profile-search-k$bits-n$count-d$distribution.csv"
      done
    done
  done
  BOUNDLIST_COUNTERS=1 taskset -c "$SIXDB_CPU" "$exe" 16 256 0 131072 1 \
    > "$SIXDB_RESULTS/$profile-search-counters.csv" 2> "$SIXDB_RESULTS/$profile-search-counters.jsonl"
  cp "$exe" "$SIXDB_RESULTS/$profile-search-binary"
  cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
done
