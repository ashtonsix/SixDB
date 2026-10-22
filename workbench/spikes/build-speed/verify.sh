#!/bin/bash
# Whole clean builds with Ninja's default -j under the adopted debug-info and
# heavy-compile pool settings, sampling host memory. CASES is a space-separated
# list of name=cmake-args (commas separate args).
set -euo pipefail
OUT=$SIXDB_RESULTS; here=workbench/spikes/build-speed
CASES=${CASES:-lines= full=-DSIXDB_DEBUG_INFO=full}
lscpu > "$OUT/lscpu.txt"; free -m > "$OUT/free.txt"
targets() {
  ninja -C "$1" -t targets all | sed -nE 's/^(((ikea|loom|orbital|engine)_[A-Za-z0-9_]*(check|validate_[A-Za-z0-9_]*))|ikea_example_[A-Za-z0-9_]+): phony$/\1/p' | grep -v "^orbital_design_check$" | sort -u
}
sample() { # minimum MemAvailable (MiB) while the build runs
  local min=999999999 a
  while :; do
    a=$(awk '/^MemAvailable/{print int($2/1024)}' /proc/meminfo)
    (( a < min )) && min=$a && echo "$min" > "$1"
    sleep 0.25
  done
}
echo "case,status,wall,cpu,min_available_mib,pool,tree_kib,ctest" > "$OUT/verify.csv"
for c in $CASES; do
  name=${c%%=*}; args=${c#*=}; dir=build/verify-$name
  rm -rf "$dir"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ${args//,/ } > "$OUT/$name.configure.log"
  pool=$(cat "$dir"/build.ninja "$dir"/CMakeFiles/rules.ninja | awk '/^pool sixdb_heavy_compile/{getline; print $3}')
  ninja -C "$dir" -t commands ikea_filters_check | grep -m1 -- 'widths_41' > "$OUT/$name.command.txt" || true
  sample "$OUT/$name.minavail" & sampler=$!
  t0=$EPOCHREALTIME; status=0
  ninja -k 0 -C "$dir" all $(targets "$dir") > "$OUT/$name.build.log" 2>&1 || status=$?
  t1=$EPOCHREALTIME
  kill $sampler; wait $sampler 2>/dev/null || true
  cp "$dir/.ninja_log" "$OUT/$name.ninja_log"
  python3 "$here/sizes.py" "$dir" "$OUT/$name.sizes.csv" || true
  ctest=skipped
  if [[ $status == 0 && ${CTEST:-1} == 1 ]]; then
    ctest --test-dir "$dir" -j "$(nproc)" -R '^(ikea|loom|orbital|engine)_' -E design \
      > "$OUT/$name.ctest.log" 2>&1 && ctest=pass || ctest=fail
  fi
  cpu=$(awk -F'\t' '!/^#/{s+=$2-$1} END{print s/1000}' "$dir/.ninja_log")
  echo "$name,$status,$(awk "BEGIN{print $t1-$t0}"),$cpu,$(cat "$OUT/$name.minavail"),$pool,$(du -sk "$dir" | cut -f1),$ctest" >> "$OUT/verify.csv"
  rm -rf "$dir"
done
sudo dmesg 2>/dev/null | grep -i -E 'out of memory|oom-kill' > "$OUT/oom.txt" || true
