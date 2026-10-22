#!/bin/bash
# Worker job: Ikea validation under both ARM tunes and ASan/UBSan SeriesPack
# checks, built from current sources with PATCH (a variant patch) applied.
set -euo pipefail
study=workbench/spikes/neon-local-transpose
jobs="${JOBS:-$SIXDB_BUILD_JOBS}"
out="$SIXDB_RESULTS"
lscpu --json > "$out/lscpu.json"
clang++-21 --version > "$out/compiler.txt"
source="$SIXDB_SOURCE/build/neon-local-transpose/candidate-source"
rm -rf "$source"
mkdir -p "$source"
tar --exclude=./build -cf - . | tar -xf - -C "$source"
patch -p1 --silent -d "$source/ikea/include" -i "$PWD/$study/$PATCH"
for tune in neoverse-v2 neoverse-v3; do
  build="$SIXDB_SOURCE/build/neon-local-transpose/validate-$tune"
  cmake -S "$source" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH=armv8-a+simd \
    -DSIXDB_TUNE="$tune" -DSIXDB_TEST_SCOPE=full > "$out/validate-$tune-configure.log"
  cmake --build "$build" --target ikea_validate -j "$jobs" > "$out/validate-$tune.log" 2>&1
done
build="$SIXDB_SOURCE/build/neon-local-transpose/asan"
cmake -S "$source" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIXDB_MARCH=armv8-a+simd \
  -DSIXDB_TUNE=neoverse-v2 -DSIXDB_TEST_SCOPE=full '-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer' \
  > "$out/asan-configure.log"
cmake --build "$build" --target ikea_seriespack_check ikea_seriespack_descriptor_check \
  -j "$jobs" > "$out/asan-build.log" 2>&1
"$build/ikea/ikea_seriespack_check" > "$out/asan-checks.txt" 2>&1
"$build/ikea/ikea_seriespack_descriptor_check" >> "$out/asan-checks.txt" 2>&1
