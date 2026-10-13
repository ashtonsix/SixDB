#!/usr/bin/env bash
set -euo pipefail

# Run on the pinned AArch64 Linux toolchain. Nothing is built inside selected/.
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_root=${SOURCE_ROOT:-"$script_dir/../../native-adapter-source"}
library_root=${LIB_ROOT:-"$source_root/build/local-controls"}
output_root=${OUTPUT_ROOT:-"$script_dir/../reproduce-output"}
mkdir -p "$output_root"
test -f "$library_root/loom/libloom.a"
test -f "$library_root/orbital/liborbital.a"

python3 - "$script_dir" "$source_root" <<'PY'
import hashlib, json, pathlib, sys
selected, source = map(pathlib.Path, sys.argv[1:])
provenance = json.loads((selected / 'provenance.json').read_text())
for name, expected in provenance['root_public_header_hashes'].items():
    actual = hashlib.sha256((source / name).read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit('source public header differs: ' + name)
PY

prototype_flags=(-std=c++23 -O3 -g -fno-fast-math -ffp-contract=off
  -march=armv8-a+simd -mtune=neoverse-v2)
for mode in 0 1 2 3; do
  clang++-21 "${prototype_flags[@]}" -DMODE="$mode" "$script_dir/prototype.cpp" \
    -o "$output_root/prototype-$mode"
  "$output_root/prototype-$mode" > "$output_root/prototype-check-$mode.txt"
  llvm-objdump-21 -d --demangle "$output_root/prototype-$mode" \
    > "$output_root/prototype-assembly-$mode.txt"
  clang++-21 "${prototype_flags[@]}" -DMODE="$mode" -S -emit-llvm \
    "$script_dir/prototype.cpp" -o "$output_root/prototype-ir-$mode.ll"
done
# The original frontend-only diagnostic used these exact narrower flags.
clang++-21 -std=c++23 -O3 -Xclang -disable-llvm-passes -DMODE=3 -S -emit-llvm \
  "$script_dir/prototype.cpp" -o "$output_root/prototype-frontend-3.ll"

production_flags=(-std=c++23 -O3 -g -DNDEBUG -DSIXDB_TUNE_NEOVERSE_V2=1
  -DSIXDB_TUNE_GENERIC=0 -DSIXDB_TUNE_GRANITE_RAPIDS=0 -DSIXDB_TUNE_ZEN5=0
  -DSIXDB_TUNE_NEOVERSE_V3=0 -fno-vectorize -fno-slp-vectorize
  -fno-fast-math -ffp-contract=off -march=armv8-a+simd -mtune=neoverse-v2
  -Iframe_unused -I"$script_dir/headers" -I"$source_root/loom/include"
  -I"$source_root/orbital/include")
for mode in 0 1; do
  clang++-21 "${production_flags[@]}" -DFRAME_ARG="$mode" "$script_dir/production.cpp" \
    "$library_root/loom/libloom.a" "$library_root/orbital/liborbital.a" \
    -o "$output_root/production-$mode"
  taskset -c 0 "$output_root/production-$mode" > "$output_root/production-check-$mode.txt"
  llvm-objdump-21 -d --demangle "$output_root/production-$mode" \
    > "$output_root/production-assembly-$mode.txt"
done
for pair in 0 1 2; do
  if test "$pair" = 1; then order=(1 0); else order=(0 1); fi
  for mode in "${order[@]}"; do
    taskset -c 0 "$output_root/production-$mode" --timing \
      > "$output_root/production-timing-$pair-$mode.txt"
  done
done
