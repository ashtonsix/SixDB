#!/bin/bash
# Same logical populations, exact current owner core, actual Polars calls.
set -euo pipefail
scan=workbench/spikes/frame-phase-1/scan
apt-get install -y -qq python3-venv > "$SIXDB_RESULTS/python-setup.txt" 2>&1
python3 -m venv build/frame-scan-python
py="$PWD/build/frame-scan-python/bin/python"
"$py" -m pip install --disable-pip-version-check 'numpy==2.3.4' 'polars==1.42.1' \
  > "$SIXDB_RESULTS/python-install.txt" 2>&1
"$py" -m pip freeze > "$SIXDB_RESULTS/python-packages.txt"
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/frame-phase-1/encoding/worker-inputs.json "$SIXDB_RESULTS/in" \
  > "$SIXDB_RESULTS/input-fetch.txt" 2>&1
cp workbench/spikes/frame-phase-1/encoding/worker-inputs.json "$SIXDB_RESULTS/input-artifact.json"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
find /sys/devices/system/cpu/cpu"$SIXDB_CPU"/cache -type f \
  \( -name size -o -name level -o -name type -o -name shared_cpu_list \) \
  -print -exec cat {} \; > "$SIXDB_RESULTS/cache.txt"
case "$(uname -m)" in
  x86_64) isas="${ISAS:-avx2 avx512}" ;;
  *) isas="${ISAS:-native}" ;;
esac
for isa in $isas; do
  case "$isa" in
    avx2) flags="-DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5" ;;
    avx512) flags="-DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5" ;;
    *) flags="" ;;
  esac
  build="build/clang/frame-scan-$isa"
  out="$SIXDB_RESULTS/$isa"
  mkdir -p "$out/binaries"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=frame-phase-1 -DCMAKE_POSITION_INDEPENDENT_CODE=ON $flags \
    > "$out/configure.txt" 2>&1
  cmake --build "$build" --target frame_scan_bridge -j "$SIXDB_BUILD_JOBS" \
    > "$out/build.txt" 2>&1
  library="$build/$scan/libframe_scan_bridge.so"
  cp "$library" "$out/binaries/"
  sha256sum "$library" > "$out/before.sha256"
  cp "$build/compile_commands.json" "$out/compile_commands.json"
  taskset -c "$SIXDB_CPU" "$py" "$scan/check.py" "$library" --output "$out/checks.json"
  taskset -c "$SIXDB_CPU" "$py" "$scan/run.py" \
    --library "$library" --inputs "$SIXDB_RESULTS/in/inputs/cols" \
    --metadata "$scan/columns.json" --output "$out/queries" --cpu "$SIXDB_CPU" \
    --trials "${TRIALS:-14}" --rows "${ROWS:-65536,524288,2999808}"
  "$py" "$scan/report.py" "$out/queries" > "$out/report.txt"
  sha256sum -c "$out/before.sha256" > "$out/after.txt"
done
echo complete > "$SIXDB_RESULTS/status.txt"
