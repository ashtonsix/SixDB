#!/usr/bin/env bash
# Qualify the runtime on the host's real base pages, rather than a policy granule.
set -euo pipefail
out="${SIXDB_RESULTS:-build/allocation-palettes/validation}"
build="build/clang/allocation-palettes-native-validation"
mkdir -p "$out"
python3 - "$out" <<'PY'
import hashlib, json, os, pathlib, platform, subprocess, sys
page = os.sysconf('SC_PAGESIZE')
expected = int(os.environ.get('PALETTE_EXPECTED_PAGE', page))
assert page == expected, f'expected native page {expected}, observed {page}'
files = [pathlib.Path('CMakeLists.txt')]
for folder in ('cmake', 'orbital', 'loom', 'ikea'):
    files += [p for p in pathlib.Path(folder).rglob('*') if p.is_file() and
              (p.suffix in ('.cpp', '.h', '.hpp', '.inc', '.cmake') or p.name == 'CMakeLists.txt')]
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(set(files))}
cpu = int(os.environ.get('SIXDB_CPU', min(os.sched_getaffinity(0))))
assert cpu in os.sched_getaffinity(0)
receipt = dict(native_page=page, kernel=platform.release(), machine=platform.machine(),
    cpu=cpu, input_digest=hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest(),
    compiler=subprocess.check_output(['clang++-21', '--version'], text=True),
    source_sha256=hashes)
pathlib.Path(sys.argv[1], 'validation-context.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(f'native {page}-byte pages; {len(hashes)} inputs; CPU {cpu}; digest {receipt["input_digest"]}')
PY
case "$(uname -m)" in
  aarch64) march=armv8.2-a+simd; tune=neoverse-v2 ;;
  x86_64) march=znver5; tune=zen5 ;;
  *) exit 2 ;;
esac
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" 2>&1 | tee "$out/configure.log"
cpu=$(python3 -c 'import os; print(os.environ.get("SIXDB_CPU", min(os.sched_getaffinity(0))))')
taskset -c "$cpu" cmake --build "$build" --target orbital_validate loom_validate \
  -j "${SIXDB_BUILD_JOBS:-1}" 2>&1 | tee "$out/validation.log"
python3 - "$out" "$build" <<'PY'
import hashlib, json, pathlib, sys
out, build = map(pathlib.Path, sys.argv[1:])
path = out / 'validation-context.json'
receipt = json.loads(path.read_text())
assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() == digest
           for p, digest in receipt['source_sha256'].items()), 'inputs changed during validation'
receipt['binary_sha256'] = {str(p.relative_to(build)): hashlib.sha256(p.read_bytes()).hexdigest()
    for module in ('orbital', 'loom') for p in sorted((build / module).glob('*_check')) if p.is_file()}
receipt['validation_log_sha256'] = hashlib.sha256((out / 'validation.log').read_bytes()).hexdigest()
receipt['passed'] = True
path.write_text(json.dumps(receipt, indent=2) + '\n')
PY
