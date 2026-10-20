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
  cmake --build "$dir" --target boundlist_cold boundlist_reads boundlist_bench -j "$SIXDB_BUILD_JOBS"
  for target in cold reads; do
    exe="$dir/workbench/spikes/boundlist/boundlist_$target"
    python3 workbench/spikes/boundlist/qualification-audit.py "$exe" "$SIXDB_RESULTS/$profile-$target-assembly.txt"
    cp "$exe" "$SIXDB_RESULTS/$profile-$target-binary"
  done
  # Complete-cycle warm/replay removes the earlier short-prefix residence
  # ambiguity. Retain earlier cells as diagnostics, rather than overwrite them.
  BOUNDLIST_READ_LARGE_ONLY=1 /usr/bin/time -v taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_reads" 4096 1048576 5 \
    > "$SIXDB_RESULTS/$profile-reads-complete-cycle.csv" 2> "$SIXDB_RESULTS/$profile-reads-complete-cycle-memory.txt"
  for maximum in 255 4095; do
    for nodes in 1 4096; do
      for span in 1 257; do
        taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_bench" "$nodes" 256 "$maximum" 131072 5 "$span" \
          > "$SIXDB_RESULTS/$profile-geometry-m$maximum-p$nodes-r$span.csv"
      done
    done
  done
  cp "$dir/workbench/spikes/boundlist/boundlist_bench" "$SIXDB_RESULTS/$profile-geometry-binary"
  for nodes in 1 4096 524288; do
    for mixed in 0 1; do
      for point in 0 1; do
        /usr/bin/time -v taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_cold" "$nodes" "$mixed" 257 "$point" 131072 5 \
          > "$SIXDB_RESULTS/$profile-covered-cold-p$nodes-m$mixed-point$point.csv" \
          2> "$SIXDB_RESULTS/$profile-covered-cold-p$nodes-m$mixed-point$point-memory.txt"
      done
    done
  done
  cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
done
