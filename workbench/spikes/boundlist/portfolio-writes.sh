#!/bin/bash
# Bounded final replay of the cursor and numeric-edit closure; read-only code is unchanged.
set -euo pipefail
case "$(uname -m)" in
 aarch64) profiles=neon; tune="${BOUNDLIST_TUNE:-neoverse-v2}" ;;
 *) profiles="avx2 avx512"; tune="${BOUNDLIST_TUNE:-zen5}" ;;
esac
for profile in $profiles; do
 case "$profile" in
 neon) march=armv8-a+simd ;;
 avx2) march=x86-64-v3 ;;
 avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
 esac
 dir="build/boundlist-portfolio/$profile"
 cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=boundlist \
  -DBOUNDLIST_PORTFOLIO_CONTROL="$PWD/workbench/spikes/boundlist/portfolio-control" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
 cmake --build "$dir" --target boundlist_portfolio_mutation boundlist_portfolio_mutation_before boundlist_portfolio_edits \
  boundlist_material ikea_boundlist_check ikea_boundlist_edits_check ikea_streampack_check \
  ikea_streampack_boundaries_check ikea_streampack_encodings_check -j "$SIXDB_BUILD_JOBS"
 for check in ikea_boundlist_check ikea_boundlist_edits_check ikea_streampack_check ikea_streampack_boundaries_check ikea_streampack_encodings_check; do
  taskset -c "$SIXDB_CPU" "$dir/ikea/$check" > "$SIXDB_RESULTS/$profile-$check.txt"
 done
 taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_material" > "$SIXDB_RESULTS/$profile-material.txt"
 cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
 for variant in before after; do
  suffix=""; if [ "$variant" = before ]; then suffix=_before; fi
  exe="$dir/workbench/spikes/boundlist/boundlist_portfolio_mutation$suffix"
  cp "$exe" "$SIXDB_RESULTS/$profile-$variant-mutation-binary"
  llvm-objdump-21 --no-show-raw-insn -Cd "$exe" > "$SIXDB_RESULTS/$profile-$variant-mutation-assembly.txt"
 done
 cp "$dir/workbench/spikes/boundlist/boundlist_portfolio_edits" "$SIXDB_RESULTS/$profile-edits-binary"
 llvm-objdump-21 --no-show-raw-insn -Cd "$dir/workbench/spikes/boundlist/boundlist_portfolio_edits" > "$SIXDB_RESULTS/$profile-edits-assembly.txt"
 for trial in 0 1 2; do
  if ((trial%2)); then order="after before"; else order="before after"; fi
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_portfolio_edits" 131071 3 > "$SIXDB_RESULTS/$profile-edits-t$trial.csv"
  for variant in $order; do
   suffix=""; if [ "$variant" = before ]; then suffix=_before; fi
   taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_portfolio_mutation$suffix" 64 3 \
    > "$SIXDB_RESULTS/$profile-$variant-mutation-t$trial.csv"
  done
 done
done
