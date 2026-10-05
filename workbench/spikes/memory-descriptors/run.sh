#!/bin/bash
set -euo pipefail

cmake -S . -B build/spikes/memory-descriptors-worker -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DSIXDB_SPIKES=memory-descriptors -DSIXDB_MARCH=x86-64-v4 -DSIXDB_TUNE=zen5 \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build build/spikes/memory-descriptors-worker \
  --target memory_descriptors_probe memory_descriptors_bench -j "$SIXDB_BUILD_JOBS" \
  > "$SIXDB_RESULTS/build.log" 2>&1
taskset -c "$SIXDB_CPU" \
  build/spikes/memory-descriptors-worker/workbench/spikes/memory-descriptors/memory_descriptors_probe \
  > "$SIXDB_RESULTS/native-checks.txt"
python3 -B workbench/spikes/memory-descriptors/lifecycle.py \
  > "$SIXDB_RESULTS/lifecycle-checks.txt"
descriptor_writer_cpu=$(python3 - "$SIXDB_CPU" <<'PY'
import os
import sys
reader = int(sys.argv[1])
others = sorted(os.sched_getaffinity(0) - {reader})
if not others:
    raise SystemExit("metadata writer needs a second allowed CPU")
print(others[0])
PY
)
taskset -c "$SIXDB_CPU" \
  build/spikes/memory-descriptors-worker/workbench/spikes/memory-descriptors/memory_descriptors_bench \
  --ms 30 --repetitions 5 > "$SIXDB_RESULTS/forward.csv"
taskset -c "$SIXDB_CPU" \
  build/spikes/memory-descriptors-worker/workbench/spikes/memory-descriptors/memory_descriptors_bench \
  --ms 30 --repetitions 5 --reverseorder > "$SIXDB_RESULTS/reverse.csv"
taskset -c "$SIXDB_CPU" \
  build/spikes/memory-descriptors-worker/workbench/spikes/memory-descriptors/memory_descriptors_bench \
  --ms 30 --repetitions 5 --metadata-writer "$descriptor_writer_cpu" \
  > "$SIXDB_RESULTS/writer-forward.csv"
taskset -c "$SIXDB_CPU" \
  build/spikes/memory-descriptors-worker/workbench/spikes/memory-descriptors/memory_descriptors_bench \
  --ms 30 --repetitions 5 --reverseorder --metadata-writer "$descriptor_writer_cpu" \
  > "$SIXDB_RESULTS/writer-reverse.csv"
python3 -B workbench/spikes/memory-descriptors/summarize.py "$SIXDB_RESULTS" \
  > "$SIXDB_RESULTS/summary-checks.txt"
lscpu > "$SIXDB_RESULTS/lscpu.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
llvm-objdump-21 -d \
  build/spikes/memory-descriptors-worker/workbench/spikes/memory-descriptors/memory_descriptors_bench \
  > "$SIXDB_RESULTS/disassembly.txt"
