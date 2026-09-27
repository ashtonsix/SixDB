#!/bin/bash
set -euo pipefail
for profile in scalar native; do
  march=x86-64-v3; tune=generic
  if [[ "$profile" == native ]]; then march=znver5; tune=zen5; fi
  build="build/ikea-neoverse-v3/x86-$profile"
  out="$SIXDB_RESULTS/$profile"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
    '-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer' > "$out/configure.log" 2>&1
  cmake --build "$build" --target ikea_bec256_check ikea_bec256_composition_check \
    ikea_example_bec256_ordinary ikea_example_bec256_native -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  for check in ikea_bec256_check ikea_bec256_composition_check ikea_example_bec256_ordinary ikea_example_bec256_native; do
    "$build/ikea/$check" >> "$out/checks.txt" 2>&1
  done
  python3 ikea/test/headers.py "$build" --module bec256 >> "$out/checks.txt" 2>&1
  cp "$build/compile_commands.json" "$out/"
done
