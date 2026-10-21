#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-wide-values
root="$SIXDB_SOURCE/build/wide-density"
output="$SIXDB_RESULTS"
mkdir -p "$output/binaries" "$output/assembly"
sudo -n apt-get update > "$output/packages.log" 2>&1
sudo -n apt-get install -y libfmt-dev >> "$output/packages.log" 2>&1
lscpu --json > "$output/lscpu.json"
clang++-21 --version > "$output/compiler.txt"
dpkg-query -W libc6 libstdc++6 libfmt-dev > "$output/packages.txt"
cmake -S . -B "$root" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0 -DNDEBUG' -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 \
  -DSIXDB_SPIKES=hashtable-wide-values -DWIDE_LOOM=ON -DWIDE_VALUES=64 \
  '-DWIDE_GEOMETRIES=32_9;32_10;32_17;32_18' > "$output/configure.log" 2>&1
cmake --build "$root" --target wide_ikea_32_9_64 wide_ikea_32_10_64 \
  wide_ikea_32_17_64 wide_ikea_32_18_64 wide_peers_32_64 \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$output/build.log" 2>&1
find "$root/$study" -maxdepth 1 -type f -executable -name 'wide_*' -exec cp {} "$output/binaries/" \;
for binary in "$output"/binaries/*; do
  llvm-objdump-21 -Cd --no-show-raw-insn "$binary" > "$output/assembly/$(basename "$binary").asm"
done
cp "$root/$study/folly-config/folly/folly-config.h" "$output/folly-config.h"
python3 "$study/density-run.py" "$output/binaries" "$output" --cpu "$SIXDB_CPU"
