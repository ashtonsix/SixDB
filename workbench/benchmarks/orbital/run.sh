#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Choose results directory or use worker.py}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
if [[ -n "${ORBITAL_READ_EXECUTIONS:-}" ]]; then
  for argument in "$@"; do
    if [[ "$argument" == --read-execution || "$argument" == --read-execution=* ]]; then
      echo 'Choose ORBITAL_READ_EXECUTIONS or --read-execution, not both' >&2
      exit 1
    fi
  done
  IFS=',' read -ra read_executions <<< "$ORBITAL_READ_EXECUTIONS"
  for execution in "${read_executions[@]}"; do
    case "$execution" in serial|native|manual|ring) ;; *) exit 1 ;; esac
  done
fi
if [[ -n "${ORBITAL_READ_ACTIVES:-}" ]]; then
  for argument in "$@"; do
    if [[ "$argument" == --read-active || "$argument" == --read-active=* ]]; then
      echo 'Choose ORBITAL_READ_ACTIVES or --read-active, not both' >&2
      exit 1
    fi
  done
  IFS=',' read -ra read_actives <<< "$ORBITAL_READ_ACTIVES"
fi
if [[ -n "${ORBITAL_CACHE_SETUPS:-}" ]]; then
  for argument in "$@"; do
    if [[ "$argument" == --cache-setup || "$argument" == --cache-setup=* ]]; then
      echo 'Choose ORBITAL_CACHE_SETUPS or --cache-setup, not both' >&2
      exit 1
    fi
  done
  IFS=',' read -ra cache_setups <<< "$ORBITAL_CACHE_SETUPS"
  for setup in "${cache_setups[@]}"; do
    case "$setup" in loaded|warm-trace) ;; *) exit 1 ;; esac
  done
fi
out="$SIXDB_RESULTS/orbital-allocation"
mkdir -p "$out"
if ! pkg-config --exists lmdb; then
  sudo -n apt-get update
  sudo -n apt-get install -y liblmdb-dev
fi
build=build/benchmarks/orbital-tooling
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_BENCHMARKS=orbital -DSIXDB_MARCH="${ORBITAL_MARCH:-znver5}" \
  -DSIXDB_TUNE="${ORBITAL_TUNE:-zen5}"
/usr/bin/time -v -o "$out/build.resources" cmake --build "$build" \
  --target orbital_allocation_bench orbital_workload_check orbital_split_check orbital_startup_check orbital_pack_growth_check orbital_lifecycle_bench \
  orbital_inventory_check orbital_request_failure_check orbital_slot_maintenance_check orbital_construction_check orbital_read_bounds_check \
  ikea_seriespack_admission_check ikea_tuplepack_gpr_check orbital_validate loom_validate -j "${SIXDB_BUILD_JOBS:-1}"
binary="$build/workbench/benchmarks/orbital/orbital_allocation_bench"
check="$build/workbench/benchmarks/orbital/orbital_workload_check"
taskset -c "$SIXDB_CPU" "$check" packed "$out/check-packed" > "$out/check-packed.log"
taskset -c "$SIXDB_CPU" "$check" packed-owner "$out/check-packed-owner" > "$out/check-packed-owner.log"
taskset -c "$SIXDB_CPU" "$check" lmdb "$out/check-lmdb" > "$out/check-lmdb.log"
taskset -c "$SIXDB_CPU" "$check" lmdb-read-batch64 "$out/check-lmdb-read-batch64" > "$out/check-lmdb-read-batch64.log"
taskset -c "$SIXDB_CPU" "$check" packed-retained "$out/check-packed-retained" > "$out/check-packed-retained.log"
taskset -c "$SIXDB_CPU" "$build/ikea/ikea_seriespack_admission_check" > "$out/check-seriespack-admission.log"
taskset -c "$SIXDB_CPU" "$build/workbench/benchmarks/orbital/orbital_pack_growth_check" > "$out/check-pack-growth.log"
taskset -c "$SIXDB_CPU" "$build/workbench/benchmarks/orbital/orbital_split_check" > "$out/check-split.log" 2>&1
taskset -c "$SIXDB_CPU" "$build/workbench/benchmarks/orbital/orbital_startup_check" > "$out/check-startup.log" 2>&1
taskset -c "$SIXDB_CPU" "$build/workbench/benchmarks/orbital/orbital_request_failure_check" "$out/check-request-failure" > "$out/check-request-failure.log" 2>&1
for fixture in slot_maintenance construction read_bounds; do
  taskset -c "$SIXDB_CPU" "$build/workbench/benchmarks/orbital/orbital_${fixture}_check" > "$out/check-${fixture}.log" 2>&1
done
taskset -c "$SIXDB_CPU" "$build/workbench/benchmarks/orbital/orbital_inventory_check" > "$out/check-inventory.log" 2>&1
taskset -c "$SIXDB_CPU" "$build/ikea/ikea_tuplepack_gpr_check" > "$out/check-tuplepack-gpr.log" 2>&1
cp "$binary" "$check" "$build/compile_commands.json" "$out/"
run_case() {
  local preparation_args=()
  if [[ -n "${ORBITAL_PREPARED_PAGES:-}" ]]; then
    preparation_args=(--prepared-pages "$ORBITAL_PREPARED_PAGES" --preparer-cpu "${ORBITAL_PREPARER_CPU:--1}" --prepared-prefill "${ORBITAL_PREPARED_PREFILL:-on}")
    for argument in "$@"; do
      case "$argument" in --prepared-pages|--preparer-cpu|--prepared-prefill|--prepared-pages=*|--preparer-cpu=*|--prepared-prefill=*)
        echo 'Choose preparation environment options or command flags, not both' >&2; return 1 ;;
      esac
    done
  fi
  python3 workbench/benchmarks/orbital/run.py "$binary" \
    --records "${ORBITAL_RECORDS:-8192,262144,4194304}" \
    --operations "${ORBITAL_OPERATIONS:-100000}" --repetitions "${ORBITAL_REPETITIONS:-3}" "${preparation_args[@]}" "$@"
}
run_conditioning() {
  local name="$1"
  shift
  if [[ -n "${ORBITAL_CACHE_SETUPS:-}" ]]; then
    for setup in "${cache_setups[@]}"; do
      run_case --output "$out/$name-$setup" --cache-setup "$setup" "$@"
    done
  else
    run_case --output "$out/$name" "$@"
  fi
}
run_execution() {
  local name="$1"
  shift
  if [[ -n "${ORBITAL_READ_ACTIVES:-}" ]]; then
    for active in "${read_actives[@]}"; do
      run_conditioning "$name-active$active" --read-active "$active" "$@"
    done
  else
    run_conditioning "$name" "$@"
  fi
}
if [[ -n "${ORBITAL_READ_EXECUTIONS:-}" ]]; then
  for execution in "${read_executions[@]}"; do
    run_execution "paired-$execution" --read-execution "$execution" "$@"
  done
else
  run_execution paired "$@"
fi
python3 workbench/benchmarks/orbital/pressure.py \
  "$build/workbench/benchmarks/orbital/orbital_lifecycle_bench" --output "$out/pressure" \
  --cpu "$SIXDB_CPU" --rounds "${ORBITAL_PRESSURE_ROUNDS:-64}" \
  --repetitions "${ORBITAL_REPETITIONS:-3}"
