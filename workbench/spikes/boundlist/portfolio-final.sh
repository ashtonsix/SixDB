#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
 aarch64) profiles=neon; tune="${BOUNDLIST_TUNE:-neoverse-v2}" ;;
 *) profiles="avx2 avx512"; tune="${BOUNDLIST_TUNE:-zen5}" ;;
esac
profiles="${BOUNDLIST_PROFILES:-$profiles}"
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
 /usr/bin/time -v -o "$SIXDB_RESULTS/$profile-build-memory.txt" cmake --build "$dir" \
  --target boundlist_portfolio boundlist_portfolio_before boundlist_portfolio_mutation boundlist_portfolio_mutation_before \
   boundlist_portfolio_edits boundlist_portfolio_cold boundlist_portfolio_cold_before boundlist_ownership_packets boundlist_portfolio_packets_before boundlist_material \
   ikea_boundlist_check ikea_boundlist_edits_check ikea_streampack_encodings_check -j "$SIXDB_BUILD_JOBS"
 for check in ikea_boundlist_check ikea_boundlist_edits_check ikea_streampack_encodings_check; do
  taskset -c "$SIXDB_CPU" "$dir/ikea/$check" > "$SIXDB_RESULTS/$profile-$check.txt"
 done
 taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_material" > "$SIXDB_RESULTS/$profile-material.txt"
 cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
 for name in portfolio portfolio_mutation portfolio_edits portfolio_cold; do
  exe="$dir/workbench/spikes/boundlist/boundlist_$name"
  cp "$exe" "$SIXDB_RESULTS/$profile-$name-binary"
  llvm-objdump-21 --no-show-raw-insn -Cd "$exe" > "$SIXDB_RESULTS/$profile-$name-assembly.txt"
 done
 for trial in 0 1 2; do
  if ((trial%2)); then order="after before"; else order="before after"; fi
  for variant in $order; do
   suffix=""; if [ "$variant" = before ]; then suffix=_before; fi
   packet=boundlist_ownership_packets
   if [ "$variant" = before ]; then packet=boundlist_portfolio_packets_before; fi
   taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/$packet" 1 131072 3 > "$SIXDB_RESULTS/$profile-$variant-packets-t$trial.csv"
   for nodes in 1 64; do
    taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_portfolio$suffix" "$nodes" 131071 3 256 \
     > "$SIXDB_RESULTS/$profile-$variant-p$nodes-t$trial.csv"
   done
   taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_portfolio$suffix" 1 131071 3 16 \
    > "$SIXDB_RESULTS/$profile-$variant-tiny-t$trial.csv"
   taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_portfolio_mutation$suffix" 32 3 \
    > "$SIXDB_RESULTS/$profile-$variant-mutation-t$trial.csv"
  done
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_portfolio_edits" 131071 3 \
   > "$SIXDB_RESULTS/$profile-edits-t$trial.csv"
 done
 for nodes in 1 524288; do
  if [ "$nodes" = 1 ]; then ops=131071; else ops=262147; fi
  for point in 0 1; do
   for variant in before after; do
    suffix=""; if [ "$variant" = before ]; then suffix=_before; fi
    /usr/bin/time -v -o "$SIXDB_RESULTS/$profile-$variant-cold-p$nodes-point$point-memory.txt" \
     taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_portfolio_cold$suffix" "$nodes" 1 257 "$point" "$ops" 3 \
     > "$SIXDB_RESULTS/$profile-$variant-cold-p$nodes-point$point.csv"
   done
  done
 done
done
