#!/bin/bash
set -euo pipefail
study=workbench/spikes/memory-characterisation
build_dir=build/adjacent-native
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
python3 - <<'PY' > "$SIXDB_RESULTS/hardware.json"
import json, os, sys
sys.path.insert(0, 'workbench/spikes/memory-characterisation')
from hardware import discover
print(json.dumps(discover(int(os.environ['SIXDB_CPU'])), indent=2))
PY
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=memory-characterisation -DSIXDB_TUNE=generic -DSIXDB_MARCH= \
  > "$SIXDB_RESULTS/configure.txt" 2>&1
targets=(memory_adjacent_probe)
if [[ "${ADJACENT_STANDALONE_ONLY:-0}" != 1 ]]; then
  targets+=(memory_adjacent_runtime loom_hardware_check)
fi
cmake --build "$build_dir" --target "${targets[@]}" \
  -j "$SIXDB_BUILD_JOBS" > "$SIXDB_RESULTS/build.txt" 2>&1
if [[ "${ADJACENT_STANDALONE_ONLY:-0}" != 1 ]]; then
  ctest --test-dir "$build_dir" -R '^loom_hardware_check$' --output-on-failure --no-tests=error \
  > "$SIXDB_RESULTS/runtime-checks.txt" 2>&1
  cp "$build_dir/workbench/spikes/memory-characterisation/memory_adjacent_runtime" "$SIXDB_RESULTS/adjacent-runtime"
  taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/adjacent-runtime" > "$SIXDB_RESULTS/runtime-forward.json"
fi
cp "$build_dir/workbench/spikes/memory-characterisation/memory_adjacent_probe" "$SIXDB_RESULTS/adjacent"
cp "$build_dir/compile_commands.json" "$SIXDB_RESULTS/compile_commands.json"
llvm-objdump-21 -dr --demangle "$SIXDB_RESULTS/adjacent" > "$SIXDB_RESULTS/adjacent.asm"
if [[ "${ADJACENT_VALIDATE_ONLY:-0}" == 1 ]]; then
  exit 0
fi
for seed in 13579 918273; do
  set +e
  taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/adjacent" --cpu "$SIXDB_CPU" \
    --trials 128 --reps 3 --seed "$seed" --mib 128 --delays 0,100,300 \
    > "$SIXDB_RESULTS/seed-$seed.csv" 2> "$SIXDB_RESULTS/seed-$seed.txt"
  probe_status=$?
  set -e
  if [[ "$probe_status" == 77 ]]; then
    python3 - "$SIXDB_RESULTS" "$seed" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
path = root / f'seed-{sys.argv[2]}.txt'
receipt = {'format': 1, 'status': 'unsupported', 'seed': int(sys.argv[2]),
           'diagnostic': path.read_text(), 'diagnostic_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
           'binary_sha256': hashlib.sha256((root / 'adjacent').read_bytes()).hexdigest(),
           'runtime_adjacent': 'unknown', 'failed_seed_measured_trials': 0,
           'qualification': 'incomplete; any earlier seed output remains diagnostic'}
(root / 'adjacent-unsupported.json').write_text(json.dumps(receipt, indent=2) + '\n')
PY
    exit 0
  fi
  if [[ "$probe_status" != 0 ]]; then
    exit "$probe_status"
  fi
done
python3 "$study/adjacent_report.py" "$SIXDB_RESULTS"
