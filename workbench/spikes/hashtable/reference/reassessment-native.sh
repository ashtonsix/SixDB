#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
uname -a > "$SIXDB_RESULTS/kernel.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
cp /proc/meminfo "$SIXDB_RESULTS/meminfo.txt"
if [[ $(uname -m) == aarch64 ]]; then tune=neoverse-v2;variants=(neon)
else tune=${HASHTABLE_TUNE:?};variants=(avx2 avx512);fi
for isa in "${variants[@]}"; do
  march=armv8-a+simd
  if [[ $isa == avx2 ]]; then march=x86-64-v3;fi
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5;else march=graniterapids;fi
  fi
  build="build/hashtable-reassessment-$isa"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
    > "$SIXDB_RESULTS/configure-$isa.log" 2>&1
  cmake --build "$build" --target hashtable_historical_bridge hashtable_lifecycle -j "${SIXDB_BUILD_JOBS:-1}" \
    > "$SIXDB_RESULTS/build-$isa.log" 2>&1
  dir="$build/workbench/spikes/hashtable/reference"
  for name in historical_bridge lifecycle; do
    exe="$dir/hashtable_$name"
    taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/check-$name-$isa.txt"
    llvm-objdump-21 -d --demangle "$exe" | gzip > "$SIXDB_RESULTS/assembly-$name-$isa.txt.gz"
    cp "$exe" "$SIXDB_RESULTS/$name-$isa"
    sha256sum "$exe" > "$SIXDB_RESULTS/binary-$name-$isa.sha256"
  done
  cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile-$isa.json"
  /usr/bin/time -v -o "$SIXDB_RESULTS/historical-bridge-$isa.resources" \
    taskset -c "$SIXDB_CPU" "$dir/hashtable_historical_bridge" 64 6 \
    > "$SIXDB_RESULTS/historical-bridge-$isa.csv"
  # Two bucket-count scales, two capacities, exactly either side of the
  # prototype's reserve threshold. These are footprint/cardinality labels;
  # they are not a substitute for measuring cache residence on this host.
  for buckets in ${HASHTABLE_BUCKET_SCALES:-256 65536}; do
    for slots in 10 16; do
      threshold=$((buckets*slots*4/5))
      for n in "$threshold" "$((threshold+1))"; do
        stem="$SIXDB_RESULTS/lifecycle-$isa-$slots-$n"
        /usr/bin/time -v -o "$stem.resources" \
          taskset -c "$SIXDB_CPU" "$dir/hashtable_lifecycle" "$n" "$slots" 8 "${HASHTABLE_REPS:-3}" \
          > "$stem.csv"
      done
    done
  done
done
