#!/bin/bash
# ASan+UBSan over the Frame sources and the fixtures this spike touches, as in
# the landing receipt: instrumented Frame objects linked with the release
# build's uninstrumented SeriesPack/core/Intervals archives.
# Usage (inside ubuntu, from the worktree): workbench/spikes/frame-seam/sanitize.sh [BUILD]
set -euo pipefail
build=${1:-build/clang/release}
out=build/seam-sanitize
mkdir -p "$out"
case "$(uname -m)" in
  x86_64) arch=x86-64-v3 ;;
  *) arch=armv8-a+simd ;;
esac
flags=(-std=c++23 -O1 -gline-tables-only -fsanitize=address,undefined -fno-sanitize-recover=all
  -fno-omit-frame-pointer -march="$arch" -Iikea/include -Iikea/test/frame)
objects=()
for src in ikea/src/frame/*.cpp ikea/src/frame/stages/*.cpp; do
  obj="$out/$(echo "$src" | tr / _).o"
  clang++-21 "${flags[@]}" -c "$src" -o "$obj" &
  objects+=("$obj")
  (( ${#objects[@]} % 2 == 0 )) && wait
done
wait
archives=("$build/ikea/libikea_seriespack.a" "$build/ikea/libikea_intervals.a" "$build/ikea/libikea_core.a")
status=0
for test in ikea/test/frame/description.cpp ikea/test/frame/manifest.cpp ikea/test/frame/group_image.cpp \
    ikea/test/frame/reader.cpp ikea/test/frame/stages.cpp ikea/test/selection.cpp ikea/test/intervals/wide.cpp \
    ikea/test/frame/span.cpp ikea/test/frame/demand.cpp; do
  exe="$out/$(echo "${test%.cpp}" | tr / _)"
  clang++-21 "${flags[@]}" "$test" "${objects[@]}" "${archives[@]}" -o "$exe"
  if "$exe" > "$exe.log" 2>&1; then
    printf 'PASS %s: %s\n' "$test" "$(tail -1 "$exe.log")"
  else
    printf 'FAIL %s\n' "$test"; tail -30 "$exe.log"; status=1
  fi
done
exit $status
