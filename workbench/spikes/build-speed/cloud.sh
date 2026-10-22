#!/bin/bash
# Clean, no-op and incremental build timings plus Clang time traces for the
# ordinary dev configuration and every module check/example target.
set -euo pipefail
J=${BUILD_JOBS:-$(nproc)}
OUT=$SIXDB_RESULTS
here=workbench/spikes/build-speed
lscpu > "$OUT/lscpu.txt"; free -g > "$OUT/free.txt"; clang++-21 --version > "$OUT/clang.txt"
now() { echo "$EPOCHREALTIME"; }
sub() { awk "BEGIN{print $1 - $2}"; }
TIME=(); if [[ -x /usr/bin/time ]]; then TIME=(/usr/bin/time -v); fi

configure() { # dir extra...
  local dir=$1; shift
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@" > "$OUT/$(basename "$dir").configure.log"
}
targets() {
  ninja -C "$1" -t targets all | sed -nE 's/^(((ikea|loom|orbital|engine)_[A-Za-z0-9_]*(check|validate_[A-Za-z0-9_]*))|ikea_example_[A-Za-z0-9_]+): phony$/\1/p' | grep -v "^orbital_design_check$" | sort -u
}
build() { # dir label
  local t0 t1; t0=$(now)
  "${TIME[@]}" ninja -k 0 -C "$1" -j "$J" all $(cat "$OUT/targets.txt") > "$OUT/$2.log" 2> "$OUT/$2.time"
  t1=$(now); echo "$2,$(sub "$t1" "$t0")" >> "$OUT/wall.csv"
}

rm -rf build/base build/trace
echo "phase,seconds" > "$OUT/wall.csv"
t0=$(now); configure build/base; t1=$(now); echo "configure,$(sub "$t1" "$t0")" >> "$OUT/wall.csv"
targets build/base > "$OUT/targets.txt"
build build/base clean
cp build/base/.ninja_log "$OUT/base.ninja_log"
ninja -C build/base -t deps > "$OUT/base.deps.txt"
ninja -C build/base -t commands all $(cat "$OUT/targets.txt") | grep -m1 -- "-o .*widths_41" > "$OUT/sample_command.txt" || true
build build/base noop
python3 "$here/sizes.py" build/base "$OUT/sizes.csv"

# Incremental rebuilds after touching representative files.
while read -r f; do
  [[ -f $f ]] || continue
  touch "$f"
  n=$(ninja -C build/base -n all $(cat "$OUT/targets.txt") | grep -c '^\[' || true)
  label="touch:$f"; t0=$(now)
  ninja -k 0 -C build/base -j "$J" all $(cat "$OUT/targets.txt") > /dev/null
  t1=$(now); echo "\"$label\",$(sub "$t1" "$t0"),$n" >> "$OUT/incremental.csv"
done < "$here/touch.txt"

# Attribution: a second clean build with Clang traces.
export SIXDB_RSS_LOG=$OUT/rss.csv; echo "output,max_rss_kib,wall,user,sys,status" > "$SIXDB_RSS_LOG"
configure build/trace -DSIXDB_TIME_TRACE=ON -DCMAKE_CXX_COMPILER_LAUNCHER=$PWD/$here/rss.sh
build build/trace trace
cp build/trace/.ninja_log "$OUT/trace.ninja_log"
python3 "$here/traces.py" build/trace "$OUT/traces"
