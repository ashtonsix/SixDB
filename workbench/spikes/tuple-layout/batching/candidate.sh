#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Choose output or use worker.py}"
output="$SIXDB_RESULTS"
SIXDB_RESULTS="$output/crossover" bash workbench/spikes/tuple-layout/batching/crossover.sh --benchmark_filter="${TUPLE_CROSS_FILTER:-cross/}"
TUPLEPACK_BROAD=1 SIXDB_RESULTS="$output/module" bash workbench/benchmarks/tuplepack/run.sh

if [[ " ${TUPLE_PROFILES:-avx2 avx512} " == *" avx2 "* ]]; then
  build=build/tuple-layout/packet-baseline
  out="$output/baseline"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES= -DSIXDB_MARCH=x86-64 -DSIXDB_TUNE=generic > "$out/configure.log" 2>&1
  cmake --build "$build" --target ikea_tuplepack_packets_check ikea_tuplepack_operations_check -j1 > "$out/build.log" 2>&1
  "$build/ikea/ikea_tuplepack_packets_check" > "$out/packets.txt"
  "$build/ikea/ikea_tuplepack_operations_check" > "$out/operations.txt"
fi

# Representative incremental edits on the worker copy; source bytes are unchanged.
for profile in ${TUPLE_PROFILES:-avx2 avx512}; do
  build="build/benchmarks/tuplepack/$profile"
  out="$output/module/$profile"
  # The preceding profile's mtime probes also invalidate this build tree.
  # Resolve those dependencies before timing a preparation-only edit.
  cmake --build "$build" --target ikea_tuplepack -j1 > "$out/library-baseline.txt" 2>&1
  touch ikea/src/tuplepack/packet_prepare.cpp
  /usr/bin/time -v -o "$out/edit-preparation.resources" cmake --build "$build" --target ikea_tuplepack -j1 > "$out/edit-preparation.txt" 2>&1
  touch ikea/include/ikea/tuplepack/detail/native/packet.h
  /usr/bin/time -v -o "$out/edit-kernel.resources" cmake --build "$build" --target ikea_tuplepack -j1 > "$out/edit-kernel.txt" 2>&1
  # Bring the example current before measuring a caller-only edit.
  cmake --build "$build" --target ikea_example_tuplepack_packets -j1 > "$out/example-baseline.txt" 2>&1
  touch ikea/examples/tuplepack/packets.cpp
  /usr/bin/time -v -o "$out/edit-caller.resources" cmake --build "$build" --target ikea_example_tuplepack_packets -j1 > "$out/edit-caller.txt" 2>&1
  cp "$build/.ninja_log" "$out/incremental.ninja-log.txt"
done
