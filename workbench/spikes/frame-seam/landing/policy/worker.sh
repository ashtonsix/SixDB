#!/bin/bash
set -euo pipefail
study=workbench/spikes/frame-seam/landing/policy
case "$(uname -m)" in
 x86_64) profiles='avx512 avx2'; tune=znver5 ;;
 *) profiles=neon; tune=neoverse-v2 ;;
esac
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
for profile in $profiles; do
 case $profile in avx512) march=znver5 ;; avx2) march=x86-64-v3 ;; neon) march=armv8-a+simd ;; esac
 out="$SIXDB_RESULTS/$profile"; mkdir -p "$out"
 flags=(-std=c++23 -O3 -gline-tables-only -march="$march" -mtune="$tune" -fno-fast-math -ffp-contract=off -falign-functions=64 -mllvm -align-all-functions=6 -mllvm -align-all-nofallthru-blocks=6)
 for arm in qualified first final; do
  case $arm in
   qualified) includes=(-I"$study/qualified/include" -Iikea/include); source="$study/qualified/stage.cpp" ;;
   first) includes=(-Iikea/include); source="$study/first.cpp" ;;
   final) includes=(-Iikea/include); source=ikea/src/frame/stage.cpp ;;
  esac
  clang++-21 "${flags[@]}" "${includes[@]}" "$study/driver.cpp" "$source" -o "$out/$arm" 2> "$out/$arm-build.txt"
 done
 python3 "$study/pair.py" "$out" > "$out/results.jsonl"
done
