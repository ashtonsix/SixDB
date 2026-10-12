#!/bin/bash
# Actual 16 KiB production geometry/VM checks; not the Linux worker runtime.
set -euo pipefail
test "$(uname -s)" = Darwin
compiler=${SIXDB_MAC_CLANGXX:-/opt/homebrew/opt/llvm@21/bin/clang++}
"$compiler" --version | head -1 | grep -Eq 'clang version 21[.]1[.]8([[:space:]]|$)'
sdk=$(xcrun --sdk macosx --show-sdk-path)
toolchain=$(cd "$(dirname "$compiler")/.." && pwd)
out=${1:-build/allocation-palettes/macos-current-qualification}
mkdir -p "$out"
python3 - "$out" "$compiler" "$sdk" <<'PY'
import hashlib, json, os, pathlib, platform, subprocess, sys
out, compiler, sdk = sys.argv[1:]
assert os.sysconf('SC_PAGESIZE') == 16384, 'requires actual 16KiB pages'
files = ['orbital/src/allocation/finite.cpp', 'orbital/src/allocation/finite.h',
         'orbital/include/orbital/types.h', 'orbital/test/finite.cpp',
         'workbench/spikes/allocation-palettes/platforms/qualify-macos.sh']
receipt = dict(native_page=16384, system=platform.platform(), sdk=sdk,
    compiler=subprocess.check_output([compiler, '--version'], text=True),
    source_sha256={p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() for p in files},
    limits=['Production finite geometry/VM only; no Linux Orbital/Loom runtime qualification',
            'Correctness checks, not timing; CPU placement uncontrolled'])
pathlib.Path(out, 'validation-context.json').write_text(json.dumps(receipt, indent=2) + '\n')
PY
for variant in release sanitize; do
  extra=(-O3)
  if [[ "$variant" == sanitize ]]; then
    extra=(-O2 -fsanitize=address,undefined -fno-omit-frame-pointer)
  fi
  "$compiler" --no-default-config -isysroot "$sdk" -std=c++23 -g \
    -fno-fast-math -ffp-contract=off -stdlib=libc++ -D_LIBCPP_DISABLE_AVAILABILITY \
    -Iorbital/include -Iorbital/src/allocation \
    -L"$toolchain/lib/c++" -Wl,-rpath,"$toolchain/lib/c++" "${extra[@]}" \
    orbital/src/allocation/finite.cpp orbital/test/finite.cpp -o "$out/finite-$variant"
  "$out/finite-$variant" 2>&1 | tee "$out/finite-$variant.log"
done
python3 - "$out" <<'PY'
import hashlib, json, pathlib, sys
out = pathlib.Path(sys.argv[1])
path = out / 'validation-context.json'
receipt = json.loads(path.read_text())
assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() == digest
           for p, digest in receipt['source_sha256'].items()), 'inputs changed during validation'
receipt['binary_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (out / 'finite-release', out / 'finite-sanitize')}
receipt['results'] = {p.name: p.read_text() for p in sorted(out.glob('*.log'))}
receipt['passed'] = True
path.write_text(json.dumps(receipt, indent=2) + '\n')
PY
