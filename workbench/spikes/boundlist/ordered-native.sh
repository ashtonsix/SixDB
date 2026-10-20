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
  cmake --build "$dir" --target boundlist_ranges -j "$SIXDB_BUILD_JOBS"
  exe="$dir/workbench/spikes/boundlist/boundlist_ranges"
  python3 workbench/spikes/boundlist/qualification-audit.py "$exe" "$SIXDB_RESULTS/$profile-ordered-assembly.txt"
  cp "$exe" "$SIXDB_RESULTS/$profile-ordered-binary"
  PYTHONPATH=workbench/tools python3 -c 'from worker_context import GroupContext; import sys; GroupContext.from_env().progress("measuring",case=sys.argv[1])' "$profile"
  for bits in 16 32 64 128; do
    for n in 256 4096; do
      for nodes in 1 64; do
        for repeat in 1 8; do
          for sparse in 0 1; do
            for point in 0 1; do
              taskset -c "$SIXDB_CPU" "$exe" "$bits" "$nodes" "$n" "$repeat" 1 257 "$sparse" "$point" 65536 3 \
                > "$SIXDB_RESULTS/$profile-ordered-k$bits-n$n-p$nodes-d$repeat-s$sparse-point$point.csv"
            done
          done
        done
      done
    done
  done
  # Wider ordered comparison without a universal SIMD/integer encoding.
  for bits in 64 128; do
    taskset -c "$SIXDB_CPU" "$exe" "$bits" 1 16384 8 1 257 1 0 32768 3 \
      > "$SIXDB_RESULTS/$profile-ordered-k$bits-n16384.csv"
  done
  python3 ikea/test/headers.py "$dir" --module boundlist > "$SIXDB_RESULTS/$profile-headers.txt"
  cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
done
