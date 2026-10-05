#!/bin/bash
set -euo pipefail

descriptor_study=workbench/spikes/memory-descriptors
descriptor_deps="$SIXDB_SOURCE/build/spikes/memory-descriptors-deps"
descriptor_baseline="$SIXDB_SOURCE/build/spikes/memory-descriptors-frozen-v1"
python3 -B "$descriptor_study/allocator-deps.py" "$descriptor_deps" \
  > "$SIXDB_RESULTS/dependencies.json"
python3 -B "$descriptor_study/prepare-baseline.py" "$descriptor_baseline" \
  > "$SIXDB_RESULTS/baseline-source.json"
cmake -S . -B build/spikes/memory-descriptors-tuned -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 -DCMAKE_C_COMPILER=clang-21 \
  -DSIXDB_SPIKES=memory-descriptors -DSIXDB_MARCH=x86-64-v4 -DSIXDB_TUNE=zen5 \
  -DSIXDB_DESCRIPTOR_ALLOCATOR_DEPS="$descriptor_deps" \
  -DSIXDB_DESCRIPTOR_BASELINE="$descriptor_baseline" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build build/spikes/memory-descriptors-tuned \
  --target memory_descriptors_grouped_probe memory_descriptors_grouped_bench \
    memory_descriptors_frozen_probe memory_descriptors_frozen_bench \
    memory_descriptors_occupancy_probe memory_descriptors_occupancy_bench \
    memory_descriptors_tuning -j "$SIXDB_BUILD_JOBS" \
  > "$SIXDB_RESULTS/build.log" 2>&1
descriptor_bin=build/spikes/memory-descriptors-tuned/workbench/spikes/memory-descriptors
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_grouped_probe" > "$SIXDB_RESULTS/grouped-checks.txt"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_frozen_probe" > "$SIXDB_RESULTS/frozen-checks.txt"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_occupancy_probe" > "$SIXDB_RESULTS/occupancy-checks.txt"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_tuning" --check > "$SIXDB_RESULTS/tuning-checks.txt"
# Reverse implementation order as well as cases. Original access/pair bodies
# are retained byte-for-byte apart from the frozen namespace substitution.
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_frozen_bench" --ms 10 --repetitions 3 \
  --source-context "$SIXDB_JOB" > "$SIXDB_RESULTS/grouped-frozen-forward.csv"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_grouped_bench" --ms 10 --repetitions 3 \
  --source-context "$SIXDB_JOB" > "$SIXDB_RESULTS/grouped-tuned-forward.csv"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_tuning" --ms 10 --repetitions 3 \
  > "$SIXDB_RESULTS/tuning-forward.csv"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_occupancy_bench" --ms 10 --repetitions 3 \
  > "$SIXDB_RESULTS/occupancy-forward.csv"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_occupancy_bench" --ms 10 --repetitions 3 --reverse \
  > "$SIXDB_RESULTS/occupancy-reverse.csv"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_tuning" --ms 10 --repetitions 3 --reverse \
  > "$SIXDB_RESULTS/tuning-reverse.csv"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_grouped_bench" --ms 10 --repetitions 3 --reverseorder \
  --source-context "$SIXDB_JOB" > "$SIXDB_RESULTS/grouped-tuned-reverse.csv"
taskset -c "$SIXDB_CPU" "$descriptor_bin/memory_descriptors_frozen_bench" --ms 10 --repetitions 3 --reverseorder \
  --source-context "$SIXDB_JOB" > "$SIXDB_RESULTS/grouped-frozen-reverse.csv"
python3 -B "$descriptor_study/tuning-summary.py" "$SIXDB_RESULTS" > "$SIXDB_RESULTS/summary-checks.txt"
mkdir -p "$SIXDB_RESULTS/read-confirmation"
SIXDB_RESULTS="$SIXDB_RESULTS/read-confirmation" bash "$descriptor_study/tune-reads.sh"
lscpu > "$SIXDB_RESULTS/lscpu.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
ldd --version > "$SIXDB_RESULTS/libc.txt"
llvm-objdump-21 -d "$descriptor_bin/memory_descriptors_grouped_bench" > "$SIXDB_RESULTS/grouped-disassembly.txt"
llvm-objdump-21 -d "$descriptor_bin/memory_descriptors_tuning" > "$SIXDB_RESULTS/tuning-disassembly.txt"
