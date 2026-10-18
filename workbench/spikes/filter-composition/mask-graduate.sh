#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) profiles="neon"; tune=neoverse-v2 ;;
  *) profiles="avx2 avx512"; tune="${FILTER_TUNE:-zen5}" ;;
esac
for profile in $profiles; do
  case "$profile" in
    neon) march=armv8-a+simd ;;
    avx2) march=x86-64-v3 ;;
    avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
  esac
  dir="build/filter-mask-graduation/$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=filter-composition
  cmake --build "$dir" --target filter_composition_runtime filter_composition_extra \
    filter_mask_codegen ikea_filters_check ikea_example_filters_ordinary \
    ikea_example_filters_composition ikea_example_filters_guarded -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" ctest --test-dir "$dir" --output-on-failure --no-tests=error \
    -R '^ikea_(filters_check|example_filters_.*)$' > "$SIXDB_RESULTS/$profile-check.txt"
  for panel in runtime extra; do
    exe="$dir/workbench/spikes/filter-composition/filter_composition_$panel"
    taskset -c "$SIXDB_CPU" "$exe" 100000 5 > "$SIXDB_RESULTS/$profile-$panel.csv" \
      2> "$SIXDB_RESULTS/$profile-$panel-check.txt"
    llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/$profile-$panel-disassembly.txt"
    python3 workbench/spikes/filter-composition/audit.py \
      "$SIXDB_RESULTS/$profile-$panel-disassembly.txt" "$SIXDB_RESULTS/$profile-$panel-audit.json"
    cp "$exe" "$SIXDB_RESULTS/$profile-$panel-binary"
  done
  llvm-objdump-21 -d --demangle \
    "$dir/workbench/spikes/filter-composition/CMakeFiles/filter_mask_codegen.dir/mask-codegen.cpp.o" \
    > "$SIXDB_RESULTS/$profile-masks.txt"
  python3 workbench/spikes/filter-composition/mask-audit.py \
    "$SIXDB_RESULTS/$profile-masks.txt" "$SIXDB_RESULTS/$profile-masks-audit.json"
  if [ "$profile" = neon ]; then
    cmake --build "$dir" --target filter_composition_neon_mask -j "$SIXDB_BUILD_JOBS"
    taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/filter-composition/filter_composition_neon_mask" \
      > "$SIXDB_RESULTS/neon-paired.csv" 2> "$SIXDB_RESULTS/neon-paired-check.txt"
  fi
done
