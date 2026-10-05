#!/bin/bash
set -euo pipefail

descriptor_deps="$SIXDB_SOURCE/build/spikes/memory-descriptors-deps"
python3 -B workbench/spikes/memory-descriptors/allocator-deps.py "$descriptor_deps" \
  > "$SIXDB_RESULTS/dependencies.json"
cmake -S . -B build/spikes/memory-descriptors-extended -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 -DCMAKE_C_COMPILER=clang-21 \
  -DSIXDB_SPIKES=memory-descriptors -DSIXDB_MARCH=x86-64-v4 -DSIXDB_TUNE=zen5 \
  -DSIXDB_DESCRIPTOR_ALLOCATOR_DEPS="$descriptor_deps" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build build/spikes/memory-descriptors-extended \
  --target memory_descriptors_grouped_probe memory_descriptors_grouped_bench \
    memory_descriptors_allocation -j "$SIXDB_BUILD_JOBS" \
  > "$SIXDB_RESULTS/build.log" 2>&1
descriptor_bin=build/spikes/memory-descriptors-extended/workbench/spikes/memory-descriptors
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_grouped_probe" \
  > "$SIXDB_RESULTS/grouped-checks.txt"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_allocation" --check \
  > "$SIXDB_RESULTS/allocation-checks.txt"
descriptor_second_cpu=$(python3 - "$SIXDB_CPU" <<'PY'
import os
import sys
others = sorted(os.sched_getaffinity(0) - {int(sys.argv[1])})
if not others:
    raise SystemExit("contention comparison needs a second allowed CPU")
print(others[0])
PY
)
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_grouped_bench" \
  --ms 20 --repetitions 3 --source-context "$SIXDB_JOB" > "$SIXDB_RESULTS/grouped-forward.csv"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_grouped_bench" \
  --ms 20 --repetitions 3 --reverseorder --source-context "$SIXDB_JOB" > "$SIXDB_RESULTS/grouped-reverse.csv"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_allocation" \
  --ms 20 --repetitions 3 --second-cpu "$descriptor_second_cpu" \
  > "$SIXDB_RESULTS/allocation-forward.csv"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_allocation" \
  --ms 20 --repetitions 3 --second-cpu "$descriptor_second_cpu" --reverseorder \
  > "$SIXDB_RESULTS/allocation-reverse.csv"
python3 -B workbench/spikes/memory-descriptors/extension-summary.py "$SIXDB_RESULTS" \
  > "$SIXDB_RESULTS/summary-checks.txt"
lscpu > "$SIXDB_RESULTS/lscpu.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
ldd --version > "$SIXDB_RESULTS/libc.txt"
llvm-objdump-21 -d "$descriptor_bin/memory_descriptors_grouped_bench" \
  > "$SIXDB_RESULTS/grouped-disassembly.txt"
llvm-objdump-21 -d "$descriptor_bin/memory_descriptors_allocation" \
  > "$SIXDB_RESULTS/allocation-disassembly.txt"
