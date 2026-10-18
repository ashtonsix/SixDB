#!/bin/bash
set -euo pipefail
tune="${FILTER_TUNE:-zen5}"
target="${FILTER_X86_TARGET:-filter_x86_index}"
for profile in avx2 avx512; do
  if [ "$profile" = avx2 ]; then march=x86-64-v3;
  elif [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi
  PYTHONPATH=workbench/tools python3 - "$profile" <<'PYCTX'
import sys
from worker_context import GroupContext
GroupContext.from_env().progress("building", profile=sys.argv[1])
PYCTX
  dir="build/filter-tiles/x86-index-$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=filter-composition
  cmake --build "$dir" --target "$target" -j "$SIXDB_BUILD_JOBS"
  exe="$dir/workbench/spikes/filter-composition/$target"
  PYTHONPATH=workbench/tools python3 - "$profile" <<'PYCTX'
import sys
from worker_context import GroupContext
GroupContext.from_env().progress("measuring", profile=sys.argv[1])
PYCTX
  for order in 0 1; do
    taskset -c "$SIXDB_CPU" "$exe" 80000 5 "$order" > "$SIXDB_RESULTS/$profile-index-$order.csv" \
      2> "$SIXDB_RESULTS/$profile-index-$order-check.txt"
  done
  llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/$profile-index-assembly.txt"
  if [ "$target" = filter_x86_confirm ]; then
    cmake --build "$dir" --target filter_x86_entropy -j "$SIXDB_BUILD_JOBS"
    for order in 0 1; do
      taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/filter-composition/filter_x86_entropy" 80000 5 "$order" \
        > "$SIXDB_RESULTS/$profile-entropy-$order.csv" 2> "$SIXDB_RESULTS/$profile-entropy-$order-check.txt"
    done
  fi
done
PYTHONPATH=workbench/tools python3 - <<'PYCTX'
from worker_context import GroupContext
GroupContext.from_env().progress("measurements_complete")
PYCTX
