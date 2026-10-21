#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
 aarch64) profiles=neon; tune=neoverse-v2 ;;
 *) profiles='avx2 avx512'; tune="${BOUNDLIST_TUNE:-zen5}" ;;
esac
for profile in $profiles; do
 case "$profile" in
  neon) march=armv8-a+simd ;;
  avx2) march=x86-64-v3 ;;
  avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
 esac
 dir="build/boundlist-api/$profile"
 cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=boundlist
 cmake --build "$dir" --target boundlist_portfolio boundlist_api_before_portfolio \
  boundlist_api_geometry boundlist_api_before_api_geometry ikea_boundlist_check ikea_boundlist_edits_check \
  ikea_streampack_encodings_check ikea_example_boundlist_ordinary ikea_example_boundlist_composition -j "$SIXDB_BUILD_JOBS"
 ctest --test-dir "$dir" -R '^ikea_(boundlist|streampack_encodings|example_boundlist)' --output-on-failure > "$SIXDB_RESULTS/$profile-check.txt"
 for pass in 0 1 2; do
  if [ "$pass" = 1 ]; then versions='before after'; else versions='after before'; fi
  for version in $versions; do
   if [ "$version" = before ]; then prefix=boundlist_api_before_; else prefix=boundlist_; fi
   taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/${prefix}api_geometry" 524288 3 > "$SIXDB_RESULTS/$profile-geometry-$version-$pass.csv"
   for nodes in 1 64 4096; do
    BOUNDLIST_PORTFOLIO_LARGE_ONLY=1 taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/${prefix}portfolio" "$nodes" "${BOUNDLIST_API_OPS:-262144}" 3 256 > "$SIXDB_RESULTS/$profile-stream-$version-$pass-$nodes.csv"
   done
  done
 done
 for exe in boundlist_api_geometry boundlist_api_before_api_geometry boundlist_portfolio boundlist_api_before_portfolio; do
  llvm-objdump-21 -d -C "$dir/workbench/spikes/boundlist/$exe" | gzip > "$SIXDB_RESULTS/$profile-$exe-assembly.txt.gz"
 done
 cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
done
