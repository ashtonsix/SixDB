#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Use worker.py or choose output}"
build=build/tuple-groups/baseline
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_MARCH=x86-64-v2 -DSIXDB_TUNE=zen5 > "$SIXDB_RESULTS/configure.log" 2>&1
/usr/bin/time -v -o "$SIXDB_RESULTS/build.resources" \
  cmake --build "$build" --target ikea_tuplepack_gpr_check ikea_tuplepack_packets_check \
    -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
"$build/ikea/ikea_tuplepack_gpr_check" > "$SIXDB_RESULTS/checks.log" 2>&1
"$build/ikea/ikea_tuplepack_packets_check" >> "$SIXDB_RESULTS/checks.log" 2>&1
python3 ikea/test/headers.py "$build" --module tuplepack >> "$SIXDB_RESULTS/checks.log" 2>&1
