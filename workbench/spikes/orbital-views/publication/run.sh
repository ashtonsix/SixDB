#!/bin/bash
set -euo pipefail
pub_output=${SIXDB_RESULTS:-build/spikes/orbital-view-publication/results}
mkdir -p "$pub_output"
pub_isa=${SIXDB_MARCH:-x86-64-v4}
pub_tune=${SIXDB_TUNE:-zen5}
pub_build=build/spikes/orbital-view-publication
cmake -S workbench/spikes/orbital-views/publication -B "$pub_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 \
  -DSIXDB_MARCH="$pub_isa" -DSIXDB_TUNE="$pub_tune" \
  > "$pub_output/configure.log" 2>&1
cmake --build "$pub_build" --target publication_probe publication_bench \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$pub_output/build.log" 2>&1
"$pub_build/publication_probe" > "$pub_output/checks.txt" 2>&1
pub_cpu=${SIXDB_CPU:-$(python3 -c 'import os; print(min(os.sched_getaffinity(0)))')}
pub_epochs=${PUBLICATION_EPOCHS:-16}
pub_max=${PUBLICATION_MAX_BYTES:-16777216}
for trial in $(seq 1 "${PUBLICATION_TRIALS:-3}"); do
  taskset -c "$pub_cpu" "$pub_build/publication_bench" "$pub_output/forward-$trial" "$pub_epochs" "$pub_max" \
    > "$pub_output/forward-$trial.log" 2>&1
  taskset -c "$pub_cpu" "$pub_build/publication_bench" "$pub_output/reverse-$trial" "$pub_epochs" "$pub_max" reverse \
    > "$pub_output/reverse-$trial.log" 2>&1
done
python3 -B workbench/spikes/orbital-views/publication/summarize.py "$pub_output" > "$pub_output/summary-checks.json"
lscpu > "$pub_output/lscpu.txt"
clang++-21 --version > "$pub_output/compiler.txt"
uname -a > "$pub_output/kernel.txt"
