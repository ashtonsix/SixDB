#!/bin/bash
# Full-scope ASan/UBSan build of the heavy checks with Ninja's default -j and
# the flag-sized pool, sampling host memory, then running the checks.
set -euo pipefail
OUT=$SIXDB_RESULTS; dir=build/asan-verify
rm -rf "$dir"
cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIXDB_TEST_SCOPE=full \
  "-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer" > "$OUT/configure.log"
cat "$dir"/build.ninja "$dir"/CMakeFiles/rules.ninja | awk '/^pool sixdb_heavy_compile/{getline; print $3}' > "$OUT/pool.txt"
min=999999999
( while :; do a=$(awk '/^MemAvailable/{print int($2/1024)}' /proc/meminfo)
    (( a < min )) && min=$a && echo "$min" > "$OUT/min_available_mib"; sleep 0.25; done ) & sampler=$!
t0=$EPOCHREALTIME; status=0
ninja -k 0 -C "$dir" ikea_filters_check ikea_seriespack_check ikea_tuplepack_packets_check \
  > "$OUT/build.log" 2>&1 || status=$?
echo "status=$status wall=$(awk "BEGIN{print $EPOCHREALTIME-$t0}")" > "$OUT/summary.txt"
kill $sampler; wait $sampler 2>/dev/null || true
if [[ $status == 0 ]]; then
  for t in ikea_filters_check ikea_seriespack_check ikea_tuplepack_packets_check; do
    "$dir/ikea/$t" > "$OUT/$t.txt" 2>&1 && echo "$t=pass" >> "$OUT/summary.txt" || echo "$t=fail" >> "$OUT/summary.txt"
  done
fi
rm -rf "$dir"
