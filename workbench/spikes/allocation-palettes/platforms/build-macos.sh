#!/bin/bash
# Build the frozen palette experiment without Linux Worker dependencies.
set -euo pipefail
test "$(uname -s)" = Darwin
compiler=${SIXDB_MAC_CLANGXX:-/opt/homebrew/opt/llvm@21/bin/clang++}
"$compiler" --version | head -1 | grep -Eq 'clang version 21[.]1[.]8([[:space:]]|$)'
sdk=$(xcrun --sdk macosx --show-sdk-path)
toolchain=$(cd "$(dirname "$compiler")/.." && pwd)
output=${1:-build/allocation-palettes/macos}
mkdir -p "$output"
shasum -a 256 workbench/spikes/allocation-palettes/backend/finite.cpp \
  workbench/spikes/allocation-palettes/backend/finite.h \
  workbench/spikes/allocation-palettes/backend/snapshot.json \
  orbital/include/orbital/types.h workbench/spikes/allocation-palettes/timing.cpp \
  > "$output/build-source.sha256"
"$compiler" --no-default-config -isysroot "$sdk" \
  -std=c++23 -O3 -g -fno-fast-math -ffp-contract=off -stdlib=libc++ \
  -D_LIBCPP_DISABLE_AVAILABILITY -Iorbital/include \
  -L"$toolchain/lib/c++" -Wl,-rpath,"$toolchain/lib/c++" \
  workbench/spikes/allocation-palettes/backend/finite.cpp \
  workbench/spikes/allocation-palettes/timing.cpp \
  -o "$output/allocation_palettes_timing"
shasum -a 256 -c "$output/build-source.sha256"
python3 - "$output" "$compiler" "$sdk" <<'PY'
import hashlib, json, os, pathlib, platform, subprocess, sys
out, compiler, sdk = sys.argv[1:]
files = ["workbench/spikes/allocation-palettes/backend/finite.cpp",
         "workbench/spikes/allocation-palettes/backend/finite.h",
         "workbench/spikes/allocation-palettes/backend/snapshot.json",
         "orbital/include/orbital/types.h", "workbench/spikes/allocation-palettes/timing.cpp"]
receipt = {
    "format": 1, "native_page_bytes": os.sysconf("SC_PAGESIZE"),
    "backend": "frozen palette experiment; independent of production Orbital",
    "backend_origin": json.loads(pathlib.Path(files[2]).read_text()),
    "system": platform.platform(), "compiler": compiler, "sdk": sdk,
    "compiler_version": subprocess.check_output([compiler, "--version"], text=True),
    "source_sha256": {p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() for p in files},
    "binary_sha256": hashlib.sha256(pathlib.Path(out, "allocation_palettes_timing").read_bytes()).hexdigest(),
    "limits": ["Experimental backend/controller results do not measure the public production allocator",
               "macOS has no Linux sched_setaffinity; hardware placement is uncontrolled",
               "Darwin reclamation is measured separately from Linux reclamation"],
}
pathlib.Path(out, "toolchain.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt, indent=2))
PY
