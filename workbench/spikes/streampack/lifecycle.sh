#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) isa=armv8.2-a+simd; tune=${STREAMPACK_TUNE:-neoverse-v3} ;;
  x86_64) isa=x86-64-v3; tune=${STREAMPACK_TUNE:-zen5} ;;
  *) exit 1 ;;
esac
build="build/clang/streampack-lifecycle-$isa-$tune"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
  -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log"
cmake --build "$build" --target streampack_lifecycle streampack_rewrite_check \
  streampack_publication_check -j "$SIXDB_BUILD_JOBS" > "$SIXDB_RESULTS/build.log" 2>&1
bin="$build/workbench/spikes/streampack"
for check in rewrite publication; do
  taskset -c "$SIXDB_CPU" "$bin/streampack_${check}_check" > "$SIXDB_RESULTS/$check-check.txt"
done
run_case() {
  local label=$1; shift
  local status=0
  taskset -c "$SIXDB_CPU" "$bin/streampack_lifecycle" "$@" > "$SIXDB_RESULTS/$label.csv" 2> "$SIXDB_RESULTS/$label.log" || status=$?
  printf '%s,%s\n' "$label" "$status" >> "$SIXDB_RESULTS/outcomes.csv"
  # Exit 2 is a recorded unsustained finite-budget trace; all other errors fail.
  [[ $status == 0 || $status == 2 ]]
}
for rep in 0 1 2; do
  for objects in 16 4096; do
    run_case "$rep-$objects-direct-stable-pressure" direct stable "$objects" 256 48 0 4 1024
    run_case "$rep-$objects-damage-stable-pressure" damage stable "$objects" 256 48 0 4 1024
    run_case "$rep-$objects-direct-traversal-pressure" direct traversal "$objects" 256 48 0 4 1024
    run_case "$rep-$objects-direct-stable-8" direct stable "$objects" 256 48 8 4 1024
    run_case "$rep-$objects-direct-traversal-8" direct traversal "$objects" 256 48 8 4 1024
  done
done
# Separate phase-accounting replay, and a budget insufficient for the prescribed
# old-reader schedule. The reader schedule and capacity never change on refusal.
for path in direct damage; do
  STREAMPACK_ACCOUNTING=1 run_case "accounting-$path" "$path" stable 4096 256 48 0 4 1024
  STREAMPACK_ACCOUNTING=1 run_case "tight-$path" "$path" stable 4096 256 48 0 4 512
done
cp "$bin/streampack_lifecycle" "$SIXDB_RESULTS/streampack_lifecycle"
sha256sum "$SIXDB_RESULTS/streampack_lifecycle" > "$SIXDB_RESULTS/binary.sha256"
llvm-objdump-21 --disassemble --demangle "$bin/streampack_lifecycle" > "$SIXDB_RESULTS/streampack_lifecycle.asm"
