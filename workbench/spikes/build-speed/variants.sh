#!/bin/bash
# Flag variants for the costliest TUs, then whole-build checks of two candidates.
set -euo pipefail
J=$(nproc); OUT=$SIXDB_RESULTS; here=workbench/spikes/build-speed
rm -rf build/v-*
targets() {
  ninja -C "$1" -t targets all | sed -nE 's/^(((ikea|loom|orbital|engine)_[A-Za-z0-9_]*(check|validate_[A-Za-z0-9_]*))|ikea_example_[A-Za-z0-9_]+): phony$/\1/p' | grep -v "^orbital_design_check$" | sort -u
}
cmake -S . -B build/v-base -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo > /dev/null
python3 "$here/variants.py" build/v-base "$OUT/variants.csv" 8 \
  test/filters/widths_57.cpp test/seriespack/read/composition.cpp test/filters/tiles.cpp \
  test/seriespack/mutation/widths_9_16.cpp test/tuplepack/packets.cpp \
  test/hashtable/value-check.cpp src/seriespack/readers.cpp

# Whole builds: baseline flags, then candidate global flag sets. Test runtime too.
for v in base:-O2\ -g gline:-O2\ -gline-tables-only O1:-O1\ -g; do
  name=${v%%:*}; flags=${v#*:}; dir=build/v-$name
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=$flags" > /dev/null
  t0=$EPOCHREALTIME
  ninja -k 0 -C "$dir" -j "$J" all $(targets "$dir") > "$OUT/$name.build.log" || true
  t1=$EPOCHREALTIME
  echo "$name,$(awk "BEGIN{print $t1-$t0}"),$(awk -F'\t' '!/^#/{s+=$2-$1} END{print s/1000}' "$dir/.ninja_log"),$(du -sk "$dir" | cut -f1)" >> "$OUT/whole.csv"
  cp "$dir/.ninja_log" "$OUT/$name.ninja_log"
  t0=$EPOCHREALTIME
  ctest --test-dir "$dir" -j "$J" -R '^(ikea|loom|orbital|engine)_' -E design > "$OUT/$name.ctest.log" 2>&1 || true
  t1=$EPOCHREALTIME
  echo "$name,ctest,$(awk "BEGIN{print $t1-$t0}")" >> "$OUT/whole.csv"
  python3 "$here/sizes.py" "$dir" "$OUT/$name.sizes.csv"
done
