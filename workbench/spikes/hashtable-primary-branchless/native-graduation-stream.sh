#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-primary-branchless
root="$SIXDB_SOURCE/build/hashtable-graduation-stream"
mkdir -p "$SIXDB_RESULTS/binaries" "$SIXDB_RESULTS/assembly" "$SIXDB_RESULTS/native"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
python3 "$study/hint-bench.py" "$root/hint-bench.cpp"
cmake -S . -B "$root/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
 -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0 -DNDEBUG' -DCMAKE_CXX_FLAGS=-DHINT_MODE=2 \
 -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-primary-branchless \
 -DPRIMARY_HINT_BENCH=ON -DMEMORY_INCLUDE_OVERRIDE= \
 -DMEMORY_BENCH_SOURCE="$root/hint-bench.cpp" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$root/build" --target primary_hint_stream_bench_0 primary_hint_stream_bench_8 ikea_hashtable_descriptor_check \
 -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/build.log" 2>&1
for v in 0 8;do
 binary="$root/build/$study/primary_hint_stream_bench_$v"
 cp "$binary" "$SIXDB_RESULTS/binaries/"
 llvm-objdump-21 -Cd --no-show-raw-insn "$binary" > "$SIXDB_RESULTS/assembly/v$v.asm"
 for seed in 713 991 1729;do
  taskset -c "$SIXDB_CPU" "$binary" "$seed" > "$SIXDB_RESULTS/native/v$v-s$seed.csv"
 done
done
ctest --test-dir "$root/build" --output-on-failure -R '^ikea_hashtable_descriptor_check$' > "$SIXDB_RESULTS/capture-check.txt" 2>&1
cp "$root/build/ikea/ikea_hashtable_descriptor_check" "$SIXDB_RESULTS/binaries/"
cp "$root/build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
sanitize="$root/sanitize"
cmake -S . -B "$sanitize" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
 -DCMAKE_CXX_FLAGS_DEBUG='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DNDEBUG' \
 -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 > "$SIXDB_RESULTS/sanitize-configure.txt" 2>&1
cmake --build "$sanitize" --target ikea_hashtable_lookup_check -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/sanitize-build.txt" 2>&1
"$sanitize/ikea/ikea_hashtable_lookup_check" > "$SIXDB_RESULTS/primary-sanitize-check.txt" 2>&1
cp "$sanitize/ikea/ikea_hashtable_lookup_check" "$SIXDB_RESULTS/binaries/"
