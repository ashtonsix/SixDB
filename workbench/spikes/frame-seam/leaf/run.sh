#!/bin/bash
# Builds leaf/exact_halves.cpp twice from one source, header-only (no
# SeriesPack library beyond its admission source): "current" against the worktree's expression.h, and
# "old" with old-include/ (the pre-repair expression.h from 38695dc4) first
# on the include path. Runs both and requires identical results.
#
#   run.sh [neon|avx2|avx512]   default neon. avx2/avx512 cross-compile
#   x86-64 (x86-64-v3 / znver5); on a non-x86 host they run under qemu
#   (correctness only, timings meaningless) when QEMU is set, e.g.
#   QEMU="qemu-x86_64 -cpu max -L /usr/x86_64-linux-gnu". Natively on
#   x86 leave QEMU empty.
# Writes build/seam-leaf/<profile>/{old,current}.jsonl and prints a summary.
set -euo pipefail
here=workbench/spikes/frame-seam/leaf
profile=${1:-neon}
out=build/seam-leaf/$profile
mkdir -p "$out"
case $profile in
  neon) arch=(-march=armv8-a+simd) ;;
  avx2) arch=(-march=x86-64-v3) ;;
  avx512) arch=(-march=znver5) ;;
  *) echo "unknown profile $profile" >&2; exit 2 ;;
esac
if [[ $profile != neon && $(uname -m) != x86_64 ]]; then
  arch=(--target=x86_64-linux-gnu "${arch[@]}")
fi
flags=(-std=c++23 -O3 -gline-tables-only -fno-fast-math -ffp-contract=off -falign-functions=64
  -mllvm -align-all-functions=6 -mllvm -align-all-nofallthru-blocks=6 "${arch[@]}")
# SeriesPack admission.cpp and core overlap.cpp (view/writer admission, no read kernels) are the
# only library sources needed; compiled with each variant include path.
clang++-21 "${flags[@]}" -w -I"$here/old-include" -Iikea/include "$here/exact_halves.cpp" ikea/src/seriespack/admission.cpp ikea/src/overlap.cpp -o "$out/old"
clang++-21 "${flags[@]}" -w -Iikea/include "$here/exact_halves.cpp" ikea/src/seriespack/admission.cpp ikea/src/overlap.cpp -o "$out/current"
run=(${QEMU:-})
"${run[@]}" "$out/old" old > "$out/old.jsonl"
"${run[@]}" "$out/current" current > "$out/current.jsonl"
python3 - "$out/old.jsonl" "$out/current.jsonl" <<'EOF'
import json, sys
old = {(r["K"], r["op"], r["density"]): r for r in map(json.loads, open(sys.argv[1]))}
cur = {(r["K"], r["op"], r["density"]): r for r in map(json.loads, open(sys.argv[2]))}
assert old.keys() == cur.keys()
for k in sorted(old):
    o, c = old[k], cur[k]
    assert (o["result_hi"], o["result_lo"]) == (c["result_hi"], c["result_lo"]), f"result differs {k}"
    print(f"K{k[0]} {k[1]:12} {k[2]:14} old {o['ns_per_row']:.4f} current {c['ns_per_row']:.4f} ns/row  ratio {c['ns_per_row'] / o['ns_per_row']:.2f}")
print("old and current results identical")
EOF
