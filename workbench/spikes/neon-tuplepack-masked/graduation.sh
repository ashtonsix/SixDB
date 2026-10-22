#!/bin/bash
# Worker job for the promoted source: Ikea validation under both ARM tunes at
# full test scope and ASan/UBSan TuplePack and filter checks. SKIP_VALIDATE=1
# runs only the sanitizer checks; their full-scope filter TUs build with
# ASAN_JOBS (default 4) because 10 parallel jobs exhausted 32 GiB.
set -euo pipefail
jobs="${JOBS:-$SIXDB_BUILD_JOBS}"
out="$SIXDB_RESULTS"
lscpu --json > "$out/lscpu.json"
clang++-21 --version > "$out/compiler.txt"
for tune in neoverse-v2 neoverse-v3; do
  [[ -z "${SKIP_VALIDATE:-}" ]] || break
  build="$SIXDB_SOURCE/build/neon-tuplepack-masked/validate-$tune"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH=armv8-a+simd \
    -DSIXDB_TUNE="$tune" -DSIXDB_TEST_SCOPE=full > "$out/validate-$tune-configure.log"
  cmake --build "$build" --target ikea_validate -j "$jobs" > "$out/validate-$tune.log" 2>&1
done
checks="ikea_tuplepack_wire_check ikea_tuplepack_transfer_check ikea_tuplepack_operations_check
  ikea_tuplepack_execution_check ikea_tuplepack_packets_check ikea_tuplepack_gpr_check
  ikea_tuplepack_ownership_check ikea_filters_check"
build="$SIXDB_SOURCE/build/neon-tuplepack-masked/asan"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIXDB_MARCH=armv8-a+simd \
  -DSIXDB_TUNE=neoverse-v2 -DSIXDB_TEST_SCOPE=full \
  '-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer' > "$out/asan-configure.log"
cmake --build "$build" --target $checks -j "${ASAN_JOBS:-4}" > "$out/asan-build.log" 2>&1
for check in $checks; do
  "$build/ikea/$check" > "$out/asan-$check.txt" 2>&1
  echo "$check passed under ASan/UBSan" >> "$out/asan-checks.txt"
done
