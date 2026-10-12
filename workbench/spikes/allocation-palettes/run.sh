#!/usr/bin/env bash
set -euo pipefail
# Run from the repository root; outputs stay in ignored storage.
out="${1:-build/allocation-palettes/results}"
mkdir -p "$out"
for mode in release sanitize; do
  build="build/clang/allocation-palettes-$mode"
  flags=()
  if [[ "$mode" == sanitize ]]; then
    flags=(-DCMAKE_BUILD_TYPE=RelWithDebInfo '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
  else
    flags=(-DCMAKE_BUILD_TYPE=Release)
  fi
  cmake -S . -B "$build" -G Ninja -DSIXDB_SPIKES=allocation-palettes "${flags[@]}"
  cmake --build "$build" --target allocation_palettes_native -j "${SIXDB_BUILD_JOBS:-2}"
  native="$build/workbench/spikes/allocation-palettes/allocation_palettes_native"
  if [[ "$mode" == release ]]; then
    "$native" --sizes > "$out/sizes.json"
    python3 -B workbench/spikes/allocation-palettes/run.py --sizes "$out/sizes.json" --output "$out"
    python3 -B workbench/spikes/allocation-palettes/growth.py --output "$out/growth.csv"
    python3 -B workbench/spikes/allocation-palettes/placement.py --sizes "$out/sizes.json" --output "$out/placement.csv"
    python3 -B workbench/spikes/allocation-palettes/recommend.py --sizes "$out/sizes.json" --output "$out"
    python3 -B workbench/spikes/allocation-palettes/recommend.py --sizes "$out/sizes.json" --output "$out" --churn-only
    python3 -B workbench/spikes/allocation-palettes/private_growth.py --sizes "$out/sizes.json" --output "$out/private-growth.csv"
    python3 -B workbench/spikes/allocation-palettes/counterexamples.py --sizes "$out/sizes.json" --output "$out/counterexamples.json"
    "$native" "$out/layouts.tsv" > "$out/native.jsonl"
    "$native" --production "$out/layouts.tsv" > "$out/production.jsonl"
    "$native" "$out/recommend-layouts.tsv" > "$out/recommend-native.jsonl"
  else
    ASAN_OPTIONS=detect_leaks=1 "$native" "$out/layouts.tsv" > "$out/native-sanitize.jsonl"
    ASAN_OPTIONS=detect_leaks=1 "$native" --production "$out/layouts.tsv" > "$out/production-sanitize.jsonl"
    ASAN_OPTIONS=detect_leaks=1 "$native" "$out/recommend-layouts.tsv" > "$out/recommend-native-sanitize.jsonl"
  fi
done
python3 -B workbench/spikes/allocation-palettes/retain.py --input "$out"
