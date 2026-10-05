#!/bin/bash
set -euo pipefail
explore_output=${SIXDB_RESULTS:-build/spikes/orbital-views/exploration-results}
mkdir -p "$explore_output"
printf 'original composed checks\n'
SIXDB_RESULTS="$explore_output/composition" bash workbench/spikes/orbital-views/check.sh
for study in control residency publication systems; do
  printf '%s: begin\n' "$study"
  SIXDB_RESULTS="$explore_output/$study" bash "workbench/spikes/orbital-views/$study/run.sh"
  printf '%s: complete\n' "$study"
done
printf 'completed-cut follow-up: begin\n'
SIXDB_RESULTS="$explore_output/cut" bash workbench/spikes/orbital-views/control/run-cut.sh
printf 'completed-cut follow-up: complete\n'
clang++-21 --version > "$explore_output/compiler.txt"
uname -a > "$explore_output/kernel.txt"
lscpu > "$explore_output/cpu.txt"
getconf PAGESIZE > "$explore_output/page-size.txt"
