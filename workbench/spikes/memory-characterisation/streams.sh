#!/bin/bash
set -euo pipefail
cmake --preset release -DSIXDB_SPIKES=memory-characterisation
cmake --build --preset release --target memory_characterisation_probe -j 2
lscpu > "$SIXDB_RESULTS/hardware.txt"
cp build/clang/release/workbench/spikes/memory-characterisation/memory_characterisation_probe "$SIXDB_RESULTS/probe"
"$SIXDB_RESULTS/probe" --cpu "$SIXDB_CPU" --suite streams --mib 256 --reps 3 --seed 13579 > "$SIXDB_RESULTS/streams.csv" 2> "$SIXDB_RESULTS/streams.txt"
