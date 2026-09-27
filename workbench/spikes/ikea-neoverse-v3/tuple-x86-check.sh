#!/bin/bash
set -euo pipefail
for profile in avx2 avx512; do
  march=x86-64-v3; tune=generic
  if [[ "$profile" == avx512 ]]; then march=znver5; tune=zen5; fi
  build="build/ikea-neoverse-v3/tuple-x86-$profile"
  out="$SIXDB_RESULTS/$profile"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
    '-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer' > "$out/configure.log" 2>&1
  cmake --build "$build" --target ikea_tuplepack_gpr_check ikea_tuplepack_packets_check ikea_tuplepack_wire_check \
    ikea_tuplepack_operations_check ikea_tuplepack_execution_check -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  for check in ikea_tuplepack_gpr_check ikea_tuplepack_packets_check ikea_tuplepack_wire_check ikea_tuplepack_operations_check ikea_tuplepack_execution_check; do
    "$build/ikea/$check" >> "$out/checks.txt" 2>&1
  done
  python3 ikea/test/headers.py "$build" --module tuplepack >> "$out/checks.txt" 2>&1
  cp "$build/compile_commands.json" "$out/"
done
