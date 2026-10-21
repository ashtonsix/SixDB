#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-wide-values
root="$SIXDB_SOURCE/build/wide-loom-native"
output="$SIXDB_RESULTS/complete-window"
mkdir -p "$output/binaries" "$output/assembly"
sudo -n apt-get update > "$output/packages.log" 2>&1
sudo -n apt-get install -y libfmt-dev >> "$output/packages.log" 2>&1
lscpu --json > "$output/lscpu.json"
clang++-21 --version > "$output/compiler.txt"
dpkg-query -W libc6 libstdc++6 libfmt-dev > "$output/packages.txt"
cmake -S . -B "$root" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0 -DNDEBUG' -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 \
  -DSIXDB_SPIKES=hashtable-wide-values -DWIDE_LOOM=ON '-DWIDE_VALUES=32;64;128' \
  '-DWIDE_GEOMETRIES=32_16;32_17;32_18;40_17' > "$output/configure.log" 2>&1
cmake --build "$root" --target wide_ikea_32_17_32 wide_ikea_32_17_64 wide_ikea_32_18_64 \
  wide_ikea_32_16_128 wide_ikea_40_17_64 wide_peers_32_32 wide_peers_32_64 \
  wide_peers_32_128 wide_peers_40_64 \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$output/build.log" 2>&1
find "$root/$study" -maxdepth 1 -type f -executable -name 'wide_*' -exec cp {} "$output/binaries/" \;
for binary in "$output"/binaries/*; do
  llvm-objdump-21 -Cd --no-show-raw-insn "$binary" > "$output/assembly/$(basename "$binary").asm"
done
cp "$root/$study/folly-config/folly/folly-config.h" "$output/folly-config.h"
staged_args=()
if [[ "${WIDE_STAGED:-0}" == 1 ]]; then staged_args+=(--staged); fi
python3 "$study/loom-run.py" "$output/binaries" "$output" --cpu "$SIXDB_CPU" "${staged_args[@]}"

# Native x86 ASan/UBSan, including actual preserve_none/musttail Ring callbacks.
cmake -S . -B "$root-sanitize" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O2 -g0 -DNDEBUG' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-wide-values \
  -DWIDE_LOOM=ON '-DWIDE_VALUES=8;64' -DWIDE_GEOMETRIES=32_9 \
  -DFETCHCONTENT_SOURCE_DIR_WIDE_BOOST="$root/_deps/wide_boost-src" \
  -DFETCHCONTENT_SOURCE_DIR_WIDE_FOLLY="$root/_deps/wide_folly-src" \
  > "$output/sanitize-configure.log" 2>&1
cmake --build "$root-sanitize" --target wide_ikea_32_9_8 wide_ikea_32_9_64 wide_peers_32_64 \
  -j "${SIXDB_BUILD_JOBS:-2}" > "$output/sanitize-build.log" 2>&1
mkdir -p "$output/sanitizer"
for v in 8 64; do
  for arm in materialized borrow slots slots32; do
    binary="$root-sanitize/$study/wide_ikea_32_9_$v"
    ASAN_OPTIONS=detect_leaks=1 "$binary" --impl "$arm" --phase check --population 1024 \
      > "$output/sanitizer/check-$arm-$v.csv"
    for hint in p u auto; do
      ASAN_OPTIONS=detect_leaks=1 "$binary" --impl "$arm" --phase all --population 1024 \
        --requests 4111 --repetitions 1 --hint "$hint" --pipeline 16 --prefetch 32 \
        > "$output/sanitizer/manual-$arm-$v-$hint.csv"
      ASAN_OPTIONS=detect_leaks=1 "$binary" --impl "$arm" --phase reads --population 1024 \
        --requests 4111 --repetitions 1 --hint "$hint" --loom 1 --prefetch 32 \
        > "$output/sanitizer/loom-$arm-$v-$hint.csv"
    done
  done
done
for arm in abseil boost f14 f14value abseil_slots boost_slots f14_slots; do
  binary="$root-sanitize/$study/wide_peers_32_64"
  ASAN_OPTIONS=detect_leaks=1 "$binary" --impl "$arm" --phase check --population 1024 \
    > "$output/sanitizer/check-$arm.csv"
  ASAN_OPTIONS=detect_leaks=1 "$binary" --impl "$arm" --phase all --population 1024 \
    --requests 4111 --repetitions 1 --loom 1 --prefetch 32 > "$output/sanitizer/loom-$arm.csv"
done
python3 - "$output/sanitizer" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]); files=list(p.glob('*.csv'))
assert len(files)==70 and all(f.stat().st_size>100 for f in files)
(p/'receipt.json').write_text(json.dumps({'checks':len(files),'asan_ubsan':'passed','actual_loom':True},indent=2)+'\n')
PY

if [[ "${WIDE_STAGED:-0}" == 1 ]]; then
  mkdir -p "$output/sanitizer-staged"
  for arm in slots slots32 abseil_slots boost_slots f14_slots; do
    if [[ "$arm" == slots || "$arm" == slots32 ]]; then
      binary="$root-sanitize/$study/wide_ikea_32_9_64"
      hints=(p u auto)
    else
      binary="$root-sanitize/$study/wide_peers_32_64"
      hints=(p)
    fi
    for hint in "${hints[@]}"; do
      for count in 1 7 17 4111; do
        for loom in 0 1; do
          ASAN_OPTIONS=detect_leaks=1 "$binary" --impl "$arm" --phase reads \
            --population 1024 --requests "$count" --repetitions 1 --hint "$hint" \
            --prefetch 32 --pipeline 16 --loom "$loom" --staged 1 \
            > "$output/sanitizer-staged/$arm-$hint-$count-$loom.csv"
        done
      done
    done
  done
  python3 - "$output/sanitizer-staged" <<'PYCODE'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]); files=list(p.glob('*.csv'))
assert len(files)==72 and all(f.stat().st_size>100 for f in files)
(p/'receipt.json').write_text(json.dumps({'checks':len(files),'asan_ubsan':'passed','actual_loom':True,'id_before_payload_staging':True},indent=2)+'\n')
PYCODE
fi
