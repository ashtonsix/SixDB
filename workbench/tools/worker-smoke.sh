#!/bin/bash
# Check configuration and a tiny compiled program without building SixDB.
set -euo pipefail
printf '%s\n' "${SMOKE_MESSAGE:-worker smoke passed}" > "$SIXDB_RESULTS/message.txt"
printf '%s\n' "$@" > "$SIXDB_RESULTS/arguments.txt"
if [[ ${SMOKE_BUILD:-1} == 0 ]]; then
  sleep "${SMOKE_SLEEP:-0}"
  exit "${SMOKE_EXIT:-0}"
fi
cmake --preset dev
cat > "$SIXDB_RESULTS/smoke.cpp" <<'CPP'
#include <bit>
#include <cstdint>
#include <iostream>
int main() {
  auto value = std::uint64_t{0x5555555555555555};
  std::cout << "popcount=" << std::popcount(value) << '\n';
  return std::popcount(value) == 32 ? 0 : 1;
}
CPP
clang++-21 -std=c++23 -O2 "$SIXDB_RESULTS/smoke.cpp" -o "$SIXDB_RESULTS/smoke"
taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/smoke" > "$SIXDB_RESULTS/check.txt"
cp build/clang/dev/CMakeCache.txt "$SIXDB_RESULTS/"
if [[ ${SMOKE_DATASET:-0} == 1 ]]; then
  python3 - <<'PY'
import os
from pathlib import Path
import sys
sys.path.insert(0, 'workbench/tools')
import datasets
from experiment import Run
root = Path.cwd()
run = Run(root, Path(os.environ['SIXDB_RESULTS']) / 'probe', {'check': 'shared worker inputs'})
error = None
try:
    prepared = run.input('inputs', datasets.get('uap-core'))
    count = len((prepared / 'rules.jsonl').read_text().splitlines())
    assert count > 0
    (run.output / 'counts.csv').write_text(f'kind,count\nrules,{count}\n')
    run.compact(['counts.csv'], [])
except Exception as exc:
    error = exc
error = run.finish(error)
if error:
    raise error
PY
fi
