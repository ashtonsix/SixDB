#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-wide-values
root="$SIXDB_SOURCE/build/hashtable-wide-native"
mkdir -p "$SIXDB_RESULTS/binaries" "$SIXDB_RESULTS/assembly" "$SIXDB_RESULTS/samples"
sudo -n apt-get update > "$SIXDB_RESULTS/packages.log" 2>&1
sudo -n apt-get install -y libfmt-dev >> "$SIXDB_RESULTS/packages.log" 2>&1
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
cat /proc/cpuinfo > "$SIXDB_RESULTS/cpuinfo.txt"
dpkg-query -W libc6 libstdc++6 libfmt-dev > "$SIXDB_RESULTS/packages.txt"
cmake -S . -B "$root" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0 -DNDEBUG' -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 \
  -DSIXDB_SPIKES=hashtable-wide-values -DWIDE_VALUES="${WIDE_VALUES:-8;64}" \
  -DWIDE_GEOMETRIES="${WIDE_GEOMETRIES:-32_9;32_11;32_16;32_17;32_18;40_17}" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$root" --target wide_all -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/build.log" 2>&1
cp "$root/$study/folly-config/folly/folly-config.h" "$SIXDB_RESULTS/folly-config.h"
find "$root/$study" -maxdepth 1 -type f -executable -name 'wide_*' -exec cp {} "$SIXDB_RESULTS/binaries/" \;
for binary in "$SIXDB_RESULTS"/binaries/*; do
  llvm-objdump-21 -Cd --no-show-raw-insn "$binary" > "$SIXDB_RESULTS/assembly/$(basename "$binary").asm"
done
python3 "$study/run.py" "$SIXDB_RESULTS/binaries" "$SIXDB_RESULTS" --mode "${WIDE_MODE:-pilot}" --cpu "$SIXDB_CPU"
if [[ "${WIDE_MODE:-pilot}" == full ]]; then
  cmake -S . -B "$root-sanitize" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS_RELEASE='-O2 -g0 -DNDEBUG' \
    -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-wide-values \
    '-DWIDE_VALUES=8;64' -DWIDE_GEOMETRIES=32_9 \
    -DFETCHCONTENT_SOURCE_DIR_WIDE_BOOST="$root/_deps/wide_boost-src" \
    -DFETCHCONTENT_SOURCE_DIR_WIDE_FOLLY="$root/_deps/wide_folly-src" \
    > "$SIXDB_RESULTS/sanitize-configure.log" 2>&1
  cmake --build "$root-sanitize" --target wide_ikea_32_9_8 wide_ikea_32_9_64 wide_peers_32_64 \
    -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/sanitize-build.log" 2>&1
  mkdir -p "$SIXDB_RESULTS/sanitizer"
  for v in 8 64; do
    for arm in materialized borrow slots slots32; do
      binary="$root-sanitize/$study/wide_ikea_32_9_$v"
      ASAN_OPTIONS=detect_leaks=1 "$binary" --impl "$arm" --phase check --population 1024 \
        > "$SIXDB_RESULTS/sanitizer/check-$arm-$v.csv"
      for hint in p u auto; do
        ASAN_OPTIONS=detect_leaks=1 "$binary" --impl "$arm" --phase all --population 1024 \
          --requests 4111 --repetitions 1 --hint "$hint" --pipeline 16 --prefetch 32 \
          > "$SIXDB_RESULTS/sanitizer/smoke-$arm-$v-$hint.csv"
      done
    done
  done
  for arm in abseil boost f14 f14value abseil_slots boost_slots; do
    ASAN_OPTIONS=detect_leaks=1 "$root-sanitize/$study/wide_peers_32_64" --impl "$arm" \
      --phase check --population 1024 > "$SIXDB_RESULTS/sanitizer/check-$arm.csv"
    ASAN_OPTIONS=detect_leaks=1 "$root-sanitize/$study/wide_peers_32_64" --impl "$arm" \
      --phase all --population 1024 --requests 4111 --repetitions 1 --pipeline 16 --prefetch 32 \
      > "$SIXDB_RESULTS/sanitizer/smoke-$arm.csv"
  done
fi
