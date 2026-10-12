#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) isas=(armv8.2-a+simd); tune=${STREAMPACK_TUNE:-neoverse-v2} ;;
  x86_64) isas=(x86-64-v3 x86-64-v4); tune=${STREAMPACK_TUNE:-zen5} ;;
  *) exit 1 ;;
esac
mkdir -p "$SIXDB_RESULTS"
python3 - "$SIXDB_RESULTS/source.json" <<'PY'
import hashlib, json, pathlib, sys
paths = [str(p) for root in ('ikea/include', 'ikea/src', 'orbital/include', 'orbital/src',
    'loom/include', 'loom/src', 'cmake') for p in pathlib.Path(root).rglob('*') if p.is_file()]
paths += ['CMakeLists.txt', 'ikea/CMakeLists.txt', 'orbital/CMakeLists.txt', 'loom/CMakeLists.txt',
    'workbench/spikes/streampack/CMakeLists.txt']
paths += [str(p) for p in pathlib.Path('workbench/spikes/streampack').glob('implementation_orbital*')]
paths += [str(p) for p in pathlib.Path('workbench/spikes/streampack').glob('implementation_loom*')]
files = {p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() for p in sorted(set(paths))}
identity = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
pathlib.Path(sys.argv[1]).write_text(json.dumps({'sha256': identity, 'files': files}, indent=2)+'\n')
PY
export SIXDB_SOURCE_ID
SIXDB_SOURCE_ID=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["sha256"])' "$SIXDB_RESULTS/source.json")
for isa in "${isas[@]}"; do
  build="build/clang/streampack-orbital-$isa-$tune"
  out="$SIXDB_RESULTS/$isa-$tune"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
    -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
  cmake --build "$build" --target streampack_implementation_orbital_check \
    streampack_implementation_orbital_bench -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  ctest --test-dir "$build" -R '^streampack_implementation_orbital_check$' \
    --output-on-failure > "$out/checks.txt" 2>&1
  bin="$build/workbench/spikes/streampack/streampack_implementation_orbital_bench"
  cp "$bin" "$out/streampack_implementation_orbital_bench"
  cp "$build/workbench/spikes/streampack/streampack_implementation_orbital_check" "$out/streampack_implementation_orbital_check"
  sha256sum "$bin" > "$out/binary.sha256"
  export SIXDB_BINARY_SHA256
  SIXDB_BINARY_SHA256=$(cut -d' ' -f1 "$out/binary.sha256")
  cp "$build/compile_commands.json" "$out/compile_commands.json"
  taskset -c "$SIXDB_CPU" "$bin" --cpu "$SIXDB_CPU" \
    --samples "${STREAMPACK_SAMPLES:-1024}" --warmup 64 --repeat 3 --retain 8 --raw \
    > "$out/samples.jsonl"
done
