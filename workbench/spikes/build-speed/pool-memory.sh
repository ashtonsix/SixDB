#!/bin/bash
# Peak RSS of each heavy-pool compile under the configurations workers use for
# validation: Release -O3 -g and Debug -O1 ASan/UBSan, both at full test scope.
set -euo pipefail
OUT=$SIXDB_RESULTS; here=$PWD/workbench/spikes/build-speed
heavy="ikea_filters_check ikea_seriespack_check ikea_tuplepack_packets_check"
for c in release asan; do
  dir=build/pool-$c; rm -rf "$dir"
  args=(-DCMAKE_BUILD_TYPE=Release)
  [[ $c == asan ]] && args=(-DCMAKE_BUILD_TYPE=Debug
    "-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer")
  cmake -S . -B "$dir" -G Ninja "${args[@]}" -DSIXDB_TEST_SCOPE=full -DSIXDB_HEAVY_COMPILE_JOBS=4 \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$here/rss.sh" > "$OUT/$c.configure.log"
  export SIXDB_RSS_LOG=$OUT/$c.rss.csv
  echo "output,max_rss_kib,wall,user,sys,status" > "$SIXDB_RSS_LOG"
  ninja -k 0 -C "$dir" -j 8 $heavy > "$OUT/$c.build.log" 2>&1 || true
  rm -rf "$dir"
done
