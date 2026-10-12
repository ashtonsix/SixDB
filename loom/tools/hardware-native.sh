#!/bin/bash
set -euo pipefail
build_dir=build/platform-native
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_TUNE=generic -DSIXDB_MARCH= > "$SIXDB_RESULTS/configure.txt" 2>&1
cmake --build "$build_dir" --target loom_example_hardware loom_hardware_check orbital_platform_check \
  -j "$SIXDB_BUILD_JOBS" > "$SIXDB_RESULTS/build.txt" 2>&1
ctest --test-dir "$build_dir" -R '^(loom_hardware_check|orbital_platform_check|loom_example_hardware)$' \
  --output-on-failure --no-tests=error > "$SIXDB_RESULTS/checks.txt" 2>&1
cp "$build_dir/loom/loom_example_hardware" "$SIXDB_RESULTS/hardware-inspect"
cp "$build_dir/compile_commands.json" "$SIXDB_RESULTS/compile_commands.json"
"$SIXDB_RESULTS/hardware-inspect" --cpu "$SIXDB_CPU" > "$SIXDB_RESULTS/reported.json"
"$SIXDB_RESULTS/hardware-inspect" --cpu "$SIXDB_CPU" --calibrate --force-miss \
  --mib 16 --budget-ms 2000 > "$SIXDB_RESULTS/calibrated.json"
python3 loom/tools/check_hardware_report.py "$SIXDB_RESULTS/reported.json" > "$SIXDB_RESULTS/report-check.txt"
python3 loom/tools/check_hardware_report.py "$SIXDB_RESULTS/calibrated.json" >> "$SIXDB_RESULTS/report-check.txt"
