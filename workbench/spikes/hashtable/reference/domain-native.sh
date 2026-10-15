#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
uname -a > "$SIXDB_RESULTS/kernel.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then tune=neoverse-v2;variants=(neon)
else tune=${HASHTABLE_TUNE:?};variants=(avx2 avx512);fi
for isa in "${variants[@]}"; do
  march=armv8-a+simd
  if [[ $isa == avx2 ]]; then march=x86-64-v3;fi
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5;else march=graniterapids;fi
  fi
  build="build/hashtable-domain-$isa"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
    > "$SIXDB_RESULTS/configure-$isa.log" 2>&1
  cmake --build "$build" --target hashtable_domain_compare hashtable_packed_key_baseline_check hashtable_single_check -j "${SIXDB_BUILD_JOBS:-1}" \
    > "$SIXDB_RESULTS/build-$isa.log" 2>&1
  dir="$build/workbench/spikes/hashtable/reference"
  taskset -c "$SIXDB_CPU" "$dir/hashtable_packed_key_baseline_check" > "$SIXDB_RESULTS/check-baseline-$isa.txt"
  taskset -c "$SIXDB_CPU" "$dir/hashtable_single_check" > "$SIXDB_RESULTS/check-single-$isa.txt"
  taskset -c "$SIXDB_CPU" "$dir/hashtable_domain_compare" check > "$SIXDB_RESULTS/check-domain-$isa.txt"
  llvm-objdump-21 -d --demangle "$dir/hashtable_domain_compare" | gzip > "$SIXDB_RESULTS/assembly-$isa.txt.gz"
  cp "$dir/hashtable_domain_compare" "$SIXDB_RESULTS/domain-$isa"
  sha256sum "$dir/hashtable_domain_compare" > "$SIXDB_RESULTS/binary-$isa.sha256"
  cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile-$isa.json"
  if [[ ${HASHTABLE_PANEL:-domain} == single || ${HASHTABLE_PANEL:-domain} == single-distinct ]]; then
    mode=single; shapes=('8 16' '16 16' '16 32' '32 32')
    if [[ $HASHTABLE_PANEL == single-distinct ]]; then mode=single-varied; shapes=('16 16' '32 32');fi
    for domain in 24 40 64; do
      for shape in "${shapes[@]}"; do
        read -r entries slots <<< "$shape"
        stem="$SIXDB_RESULTS/$HASHTABLE_PANEL-$isa-$domain-$entries-$slots"
        /usr/bin/time -v -o "$stem.resources" \
          taskset -c "$SIXDB_CPU" "$dir/hashtable_domain_compare" "$mode" "$entries" 256 6 128 "$domain" "$slots" > "$stem.csv"
      done
    done
    continue
  fi
  if [[ ${HASHTABLE_PANEL:-domain} == bridge ]]; then
    stem="$SIXDB_RESULTS/hash-bridge-$isa"
    /usr/bin/time -v -o "$stem.resources" \
      taskset -c "$SIXDB_CPU" "$dir/hashtable_domain_compare" bridge 4096 1 10 256 \
      > "$stem.csv" 2> "$stem-probes.csv"
    continue
  fi
  # Same 4096 entries as one table and 256 independent small tables; one extra
  # smaller footprint. These labels do not assert physical cache residence.
  for domain in 24 40 64; do
    for shape in '256 1' '4096 1' '16 256'; do
      read -r entries tables <<< "$shape"
      stem="$SIXDB_RESULTS/domain-$isa-$domain-$entries-$tables"
      /usr/bin/time -v -o "$stem.resources" \
        taskset -c "$SIXDB_CPU" "$dir/hashtable_domain_compare" \
        "$domain" "$entries" "$tables" 8 128 > "$stem.csv"
    done
  done
done
