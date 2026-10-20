#!/bin/bash
set -euo pipefail
out="$SIXDB_RESULTS"
build="$SIXDB_SOURCE/build/hashtable-memory-native"
san="$SIXDB_SOURCE/build/hashtable-memory-sanitize"
study=workbench/spikes/hashtable-boost-adoption
clang++-21 --version > "$out/compiler.txt"
lscpu --json > "$out/lscpu.json"
cmake -S . -B "$san" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS_DEBUG='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DNDEBUG' \
  -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-boost-adoption \
  -DMEMORY_CASES='40_10;40_11' -DMEMORY_WIDTHS=40 -DMEMORY_VALUES='0;8' \
  > "$out/sanitize-configure.log" 2>&1
cmake --build "$san" --target memory_all -j "${SIXDB_BUILD_JOBS:-2}" \
  > "$out/sanitize-build.log" 2>&1
for v in 0 8; do
  for seed in 713 991 1729; do
    for arm in ikea_40_10 ikea_40_11 peers_40; do
      bin="$san/$study/memory_${arm}_${v}"
      if [[ "$arm" == peers* ]]; then impls='boost abseil'; else impls=ikea; fi
      for impl in $impls; do
        taskset -c "$SIXDB_CPU" "$bin" --impl "$impl" --n 13926 --reserve 27852 \
          --seed "$seed" --requests 4097 --repetitions 1 --churn 3 \
          > "$out/check-${arm}-${v}-${impl}-${seed}.csv"
      done
    done
  done
done
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0 -DNDEBUG' -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-boost-adoption \
  > "$out/configure.log" 2>&1
cmake --build "$build" --target memory_all -j "${SIXDB_BUILD_JOBS:-2}" \
  > "$out/build.log" 2>&1
cp "$build/compile_commands.json" "$out/compile.json"
mkdir -p "$out/binaries"
cp "$build/$study"/memory_ikea_* "$build/$study"/memory_peers_* "$out/binaries/"
python3 "$study/memory-run.py" --build "$build" --output "$out/native" --cpu "$SIXDB_CPU"
