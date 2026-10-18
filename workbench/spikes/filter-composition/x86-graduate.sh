#!/bin/bash
set -euo pipefail
export FILTER_X86_TARGET=filter_x86_confirm
bash workbench/spikes/filter-composition/x86-index-native.sh
tune="${FILTER_TUNE:-zen5}"
if [ "$tune" = zen5 ]; then cpu=znver5; policy=-DSIXDB_TUNE_ZEN5=1; else cpu=graniterapids; policy=-DSIXDB_TUNE_GRANITE_RAPIDS=1; fi
for profile in avx2 avx512; do
  if [ "$profile" = avx2 ]; then march=x86-64-v3; else march="$cpu"; fi
  dir="build/filter-tiles/x86-index-$profile"
  cmake --build "$dir" --target ikea_filters_check ikea_example_filters_ordinary -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/ikea/ikea_filters_check" > "$SIXDB_RESULTS/$profile-check.txt"
  taskset -c "$SIXDB_CPU" "$dir/ikea/ikea_example_filters_ordinary" > "$SIXDB_RESULTS/$profile-example.txt"
  clang++-21 -std=c++23 -O3 -march="$march" -mtune="$cpu" "$policy" -fno-vectorize -fno-slp-vectorize \
    -Iikea/include -S -masm=intel workbench/spikes/filter-composition/x86-index-codegen.cpp \
    -o "$SIXDB_RESULTS/$profile-codegen.s"
  clang++-21 -std=c++23 -O1 -g0 -march="$march" -mtune="$cpu" "$policy" \
    -fsanitize=address,undefined -fno-omit-frame-pointer -I. -Iikea/include \
    workbench/spikes/filter-composition/x86-sanitize.cpp ikea/test/filters/local_equal.cpp \
    "$dir/ikea/libikea_seriespack.a" "$dir/ikea/libikea_core.a" -o "$SIXDB_RESULTS/$profile-sanitize"
  taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/$profile-sanitize" > "$SIXDB_RESULTS/$profile-sanitize.txt"
done
# GFNI also has a non-AVX512 arithmetic lowering; exercise its exact mask,
# including arbitrary ignored upper planes, on the same native machines.
clang++-21 -std=c++23 -O3 -march=x86-64-v3 -mgfni -Iikea/include \
  ikea/test/filters/local_equal.cpp -x c++ - -o "$SIXDB_RESULTS/gfni-avx2-check" <<'CPP'
#include <cstdio>
void check_local_equal();
int main() {check_local_equal();puts("GFNI without AVX512: all prepared queries and masks passed");}
CPP
"$SIXDB_RESULTS/gfni-avx2-check" > "$SIXDB_RESULTS/gfni-avx2-check.txt"
