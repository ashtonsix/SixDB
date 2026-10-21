#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-primary-branchless
root="$SIXDB_SOURCE/build/hashtable-primary-qualify"
modes=("$@")
if [[ ${#modes[@]} == 0 ]]; then modes=(callback result dummy); fi
for variant in "${modes[@]}"; do
 overlay="$root/$variant/include"
 python3 "$study/overlay.py" "$overlay" --records "$SIXDB_RESULTS" --mode "$variant"
 build="$root/$variant/sanitize"
 cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
   -DCMAKE_CXX_FLAGS_DEBUG='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DNDEBUG' \
   -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-primary-branchless \
   -DMEMORY_INCLUDE_OVERRIDE="$overlay" > "$SIXDB_RESULTS/$variant-configure.log" 2>&1
 cmake --build "$build" --target primary_branchless_check -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/$variant-build.log" 2>&1
 "$build/$study/primary_branchless_check" > "$SIXDB_RESULTS/$variant-check.log" 2>&1
 done
