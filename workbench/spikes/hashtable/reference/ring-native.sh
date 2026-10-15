#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
uname -a > "$SIXDB_RESULTS/kernel.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
cp /proc/meminfo "$SIXDB_RESULTS/meminfo.txt"
if [[ $(uname -m) == aarch64 ]]; then tune=neoverse-v2; variants=(neon)
else tune=${HASHTABLE_TUNE:?}; variants=(avx2 avx512); fi
for isa in "${variants[@]}"; do
  march=armv8-a+simd
  if [[ $isa == avx2 ]]; then march=x86-64-v3; fi
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
  build="build/hashtable-ring-$isa"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
    > "$SIXDB_RESULTS/configure-$isa.log" 2>&1
  cmake --build "$build" --target hashtable_ring hashtable_compare hashtable_width_check loom_example_hardware -j "${SIXDB_BUILD_JOBS:-1}" \
    > "$SIXDB_RESULTS/build-$isa.log" 2>&1
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/reference/hashtable_width_check" > "$SIXDB_RESULTS/width-check-$isa.txt"
  # Full cold policy provenance, same affinity/context, with calibration disabled.
  taskset -c "$SIXDB_CPU" "$build/loom/loom_example_hardware" > "$SIXDB_RESULTS/hardware-$isa.json"
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/reference/hashtable_compare" layouts16 7 > "$SIXDB_RESULTS/layouts16-$isa.csv"
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/reference/hashtable_compare" 4096 7 16 > "$SIXDB_RESULTS/row-word-$isa.csv"
  if [[ ${HASHTABLE_QUOTIENT:-0} == 1 ]]; then
    taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/reference/hashtable_compare" quotient16 7 > "$SIXDB_RESULTS/quotient16-$isa.csv"
  fi
  exe="$build/workbench/spikes/hashtable/reference/hashtable_ring"
  llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/assembly-$isa.txt"
  cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile-$isa.json"
  for n in ${HASHTABLE_SIZES:-128 4096 65536 1048576 4194304}; do
    for chains in 1 8 32; do
      for slots in ${HASHTABLE_BUCKET_SLOTS:-16}; do
      stem="$SIXDB_RESULTS/$isa-$n-$chains-$slots"
      /usr/bin/time -v -o "$stem.resources" taskset -c "$SIXDB_CPU" "$exe" "$n" "$chains" "${HASHTABLE_REPS:-5}" "$slots" > "$stem.csv" 2> "$stem.context"
      python3 - "$SIXDB_RESULTS/hardware-$isa.json" "$stem.context" <<'PY'
import json,sys
hardware=json.load(open(sys.argv[1]))
context=dict(line.rstrip().split('=',1) for line in open(sys.argv[2]))
assert hardware['behavior_context']==context['hardware_context']
assert not hardware['calibration_enabled'] and context['calibration_enabled']=='false'
PY
      done
    done
  done
done
