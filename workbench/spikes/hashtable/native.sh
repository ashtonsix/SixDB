#!/bin/bash
set -euo pipefail
# Worker input is already captured. This probe makes no complete-table claim.
case "${HASHTABLE_TUNE:?}" in
  zen5|granite-rapids)
    march=x86-64-v3
    ;;
  neoverse-v2) march=armv8-a+simd ;;
  *) echo "Unsupported tuning $HASHTABLE_TUNE" >&2; exit 2 ;;
esac
for isa in base wide; do
  if [[ "$isa" == wide ]]; then
    [[ "$HASHTABLE_TUNE" != neoverse-v2 ]] || continue
    march=x86-64-v4
  fi
  build="build/hashtable/$isa"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$HASHTABLE_TUNE"
  cmake --build "$build" --target hashtable_bucket hashtable_codegen -j "$SIXDB_BUILD_JOBS"
  exe="$build/workbench/spikes/hashtable/hashtable_bucket"
  taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/$isa-check.txt"
  for buckets in 16 4096; do
    taskset -c "$SIXDB_CPU" "$exe" "$buckets" 1000000 > "$SIXDB_RESULTS/$isa-$buckets.csv"
  done
  llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/$isa-disassembly.txt"
  cp "$build/compile_commands.json" "$SIXDB_RESULTS/$isa-compile_commands.json"
  cp "$build/workbench/spikes/hashtable/CMakeFiles/hashtable_codegen.dir/codegen.cpp.o" \
    "$SIXDB_RESULTS/$isa-codegen.o"
done
uname -a > "$SIXDB_RESULTS/hardware.txt"
lscpu >> "$SIXDB_RESULTS/hardware.txt"
sha256sum workbench/spikes/hashtable/{bucket.cpp,bucket.h,codegen.cpp,native.sh} \
  > "$SIXDB_RESULTS/sources.sha256"
