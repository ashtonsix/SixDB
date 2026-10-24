#!/bin/bash
# Kernel checks per ISA on a worker: the Ikea SeriesPack and filter check targets, then the
# spike's operation checks (kernels.cpp: sum, count, filter words, partial
# ranges) and boundary checks (bounds.cpp: range reads ending at a guard page,
# masked sums mixing full and partial groups).
# Any failed build or check fails the job; summary.txt names it.
# Usage: worker.py run workbench/spikes/frame-phase-2/seriespack/checks/worker.sh --machine zen5|neoverse-v2
set -uo pipefail
case "$(uname -m)" in
  x86_64) isas="${ISAS:-avx2 avx512}" ;;
  *) isas="${ISAS:-native}" ;;
esac
dir=workbench/spikes/frame-phase-2/seriespack/checks
status=0
for isa in $isas; do
  case "$isa" in
    avx2) flags="-DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5"; march=x86-64-v3 ;;
    avx512) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5"; march=znver5 ;;
    *) flags=""; march=native ;;
  esac
  build="build/clang/frame-checks-$isa"
  out="$SIXDB_RESULTS/$isa"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_TEST_SCOPE="${TEST_SCOPE:-fast}" $flags > "$out/configure.txt" 2>&1
  targets="ikea_seriespack_check ikea_seriespack_admission_check ikea_seriespack_descriptor_check ikea_seriespack_ownership_check ikea_filters_check"
  cmake --build "$build" --target $targets -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1 || { echo "build failed" >> "$out/summary.txt"; status=1; }
  for t in $targets; do
    check=$(find "$build" -name "$t" -type f | head -1)
    if "$check" > "$out/$t.txt" 2>&1; then echo "$t: ok"; else echo "$t: FAILED"; status=1; fi >> "$out/summary.txt"
  done
  libs="$(find "$build" -name libikea_seriespack.a | head -1) $(find "$build" -name libikea_core.a | head -1)"
  for t in kernels bounds; do
    if clang++-21 -std=c++23 -O2 -march=$march -I ikea/include "$dir/$t.cpp" $libs -o "$build/$t" > "$out/$t.build.txt" 2>&1 &&
      "$build/$t" > "$out/$t.txt" 2>&1; then echo "$t: ok"; else echo "$t: FAILED"; status=1; fi >> "$out/summary.txt"
  done
done
exit $status
