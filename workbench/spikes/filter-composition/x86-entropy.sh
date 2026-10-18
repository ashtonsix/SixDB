#!/bin/bash
set -euo pipefail
export FILTER_X86_TARGET=filter_x86_entropy
bash workbench/spikes/filter-composition/x86-index-native.sh
for profile in avx2 avx512; do
  dir="build/filter-tiles/x86-index-$profile"
  cmake --build "$dir" --target ikea_filters_check -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/ikea/ikea_filters_check" > "$SIXDB_RESULTS/$profile-check.txt"
done
