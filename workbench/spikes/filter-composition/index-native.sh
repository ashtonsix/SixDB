#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) profiles=neon; tune=neoverse-v2 ;;
  *) profiles="avx2 avx512"; tune="${FILTER_TUNE:-zen5}" ;;
esac
for profile in $profiles; do
  case "$profile" in
    neon) march=armv8-a+simd ;;
    avx2) march=x86-64-v3 ;;
    avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
  esac
  dir="build/filter-tiles/$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=filter-composition
  cmake --build "$dir" --target filter_indexof -j "$SIXDB_BUILD_JOBS"
  if [ "$profile" = avx512 ]; then
    cmake --build "$dir" --target filter_contains -j "$SIXDB_BUILD_JOBS"
    taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/filter-composition/filter_contains" > "$SIXDB_RESULTS/contains-check.txt"
    llvm-objdump-21 -d --disassemble-symbols=contains_local_prepared,contains6_runtime \
      "$dir/workbench/spikes/filter-composition/filter_contains" > "$SIXDB_RESULTS/contains-codegen.txt"
  fi
  exe="$dir/workbench/spikes/filter-composition/filter_indexof"
  for order in 0 1; do
    taskset -c "$SIXDB_CPU" "$exe" 60000 5 "$order" > "$SIXDB_RESULTS/$profile-index-$order.csv" \
      2> "$SIXDB_RESULTS/$profile-index-$order-check.txt"
  done
  llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/$profile-index-assembly.txt"
  python3 - "$SIXDB_RESULTS/$profile-index-assembly.txt" "$SIXDB_RESULTS/$profile-index-selected.txt" <<'PY'
import pathlib,sys
text=pathlib.Path(sys.argv[1]).read_text().splitlines();out=[];keep=False
for line in text:
    if line and not line[0].isspace() and line.endswith('>:'):
        keep='index_probe::measure<30u, 16u,' in line
    if keep:out.append(line)
assert out,'selected complete K30 consumers missing'
pathlib.Path(sys.argv[2]).write_text('\n'.join(out)+'\n')
PY
done
