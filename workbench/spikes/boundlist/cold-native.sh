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
  cmake --build "$dir" --target boundlist_cold -j "$SIXDB_BUILD_JOBS"
  python3 workbench/spikes/boundlist/qualification-audit.py "$dir/workbench/spikes/boundlist/boundlist_cold" "$SIXDB_RESULTS/$profile-cold-assembly.txt"
  # More than four requests per source header on average, with a random
  # working set; on Granite its compact operands may fit the advertised LLC. A short
  # reused prefix of131072 requests did not establish DRAM service.
  for mixed in 0 1; do
    for point in 0 1; do
      /usr/bin/time -v taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_cold" 524288 "$mixed" 257 "$point" 2097152 3 \
        > "$SIXDB_RESULTS/$profile-long-cold-m$mixed-point$point.csv" \
        2> "$SIXDB_RESULTS/$profile-long-cold-m$mixed-point$point-memory.txt"
    done
  done
  cp "$dir/workbench/spikes/boundlist/boundlist_cold" "$SIXDB_RESULTS/$profile-cold-binary"
  cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
done
