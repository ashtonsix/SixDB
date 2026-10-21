#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-boost-adoption
build_root="$SIXDB_SOURCE/build/hashtable-speculation"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
mkdir -p "$SIXDB_RESULTS/binaries" "$SIXDB_RESULTS/assembly"
for variant in base lfence cpuid; do
  overlay="$build_root/$variant/include"
  if [[ "$variant" != base ]]; then
    mkdir -p "$overlay/ikea"
    cp -R ikea/include/ikea/hashtable "$overlay/ikea/"
    python3 - "$overlay/ikea/hashtable/table.h" "$variant" "$SIXDB_RESULTS" <<'PY'
import difflib,hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]);variant=sys.argv[2];out=Path(sys.argv[3]);before=p.read_text()
needle='[[gnu::noinline, gnu::pure]] overflow_result read_overflow(word key, word swiss) const {\n'
assert before.count(needle)==1
addition=('        asm volatile("lfence" : : : "memory");\n' if variant=='lfence' else
          '        unsigned cpuid_ax = 0, cpuid_bx, cpuid_cx, cpuid_dx;\n        asm volatile("cpuid" : "+a"(cpuid_ax), "=b"(cpuid_bx), "=c"(cpuid_cx), "=d"(cpuid_dx) : : "memory");\n')
after=before.replace(needle,needle+addition);p.write_text(after)
(out/(variant+'-patch.diff')).write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='maintained/table.h',tofile=variant+'/table.h')))
(out/(variant+'-overlay.json')).write_text(json.dumps(dict(before_sha256=hashlib.sha256(before.encode()).hexdigest(),after_sha256=hashlib.sha256(after.encode()).hexdigest(),change='barrier at read_overflow entry only'),indent=2)+'\n')
PY
  fi
  override=''
  if [[ "$variant" != base ]]; then override="$overlay"; fi
  build="$build_root/$variant/build"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0 -DNDEBUG' -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-boost-adoption \
    -DMEMORY_CASES='32_11;40_11;40_12' -DMEMORY_WIDTHS=32 -DMEMORY_VALUES='0;8' \
    -DMEMORY_INCLUDE_OVERRIDE="$override" \
    > "$SIXDB_RESULTS/$variant-configure.log" 2>&1
  targets='memory_ikea_32_11_0 memory_ikea_32_11_8 memory_ikea_40_12_0 memory_ikea_40_11_8'
  if [[ "$variant" == cpuid ]]; then targets='memory_ikea_40_12_0'; fi
  cmake --build "$build" --target $targets -j "${SIXDB_BUILD_JOBS:-2}" \
    > "$SIXDB_RESULTS/$variant-build.log" 2>&1
  cp "$build/compile_commands.json" "$SIXDB_RESULTS/$variant-compile.json"
  for target in $targets; do
    cp "$build/$study/$target" "$SIXDB_RESULTS/binaries/$variant-$target"
    llvm-objdump-21 -Cd --no-show-raw-insn "$build/$study/$target" \
      > "$SIXDB_RESULTS/assembly/$variant-$target.asm"
  done
done
python3 "$study/overflow-speculation-run.py" --binaries "$SIXDB_RESULTS/binaries" \
  --output "$SIXDB_RESULTS/native" --cpu "$SIXDB_CPU"
