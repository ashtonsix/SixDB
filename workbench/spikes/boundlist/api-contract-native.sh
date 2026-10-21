#!/bin/bash
# Recheck the two generic-domain fixes and certify covered consumers' exact code.
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
 for exe in boundlist_portfolio boundlist_api_geometry; do
  test -x "$dir/workbench/spikes/boundlist/$exe"
  sha256sum "$dir/workbench/spikes/boundlist/$exe" >> "$SIXDB_RESULTS/$profile-before.sha256"
 done
 cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=boundlist
 cmake --build "$dir" --target boundlist_portfolio boundlist_api_geometry ikea_boundlist_check ikea_boundlist_edits_check -j "$SIXDB_BUILD_JOBS"
 ctest --test-dir "$dir" -R '^ikea_boundlist' --output-on-failure > "$SIXDB_RESULTS/$profile-check.txt"
 for exe in boundlist_portfolio boundlist_api_geometry; do
  sha256sum "$dir/workbench/spikes/boundlist/$exe" >> "$SIXDB_RESULTS/$profile-after.sha256"
 done
 diff -u "$SIXDB_RESULTS/$profile-before.sha256" "$SIXDB_RESULTS/$profile-after.sha256" > "$SIXDB_RESULTS/$profile-binary-equivalence.txt"
done
