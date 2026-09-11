# Shared head encoding

This isolated experiment asks how much of the headed encode cost can be
recovered by sharing input extraction and compacting stores. Production sources
are unchanged. The benchmark compares separate H16 passes with a shared pass
that writes the same independent high-byte-first head0/head1 planes.

`heads.h` has four arms per native target:

- **separate:** the existing fixed-tile plane traversal, twice for H16.
- **joint_tile:** one source traversal with the same logical tile/native lanes;
  the compiler chooses unrolling normally.
- **joint_compact:** sixteen values on AVX2 or thirty-two on AVX512, projected
  into sixteen-bit working lanes before emitting the two byte planes.
- **joint_fulltile_control:** the joint tile with explicit full inner-loop
  unrolling, solely to expose code-grain effects.

Both heads have independent bases and strides. Dense coalescing requires every
used plane to be dense; otherwise processing respects individual tiles. An
incomplete input contains exactly n readable source values; final head packets
are writable and get zero slack. H8 ignores head1. Narrow unsigned inputs are
zero-extended logically, and bits above the selected head bytes are discarded.

The full-feature K17/H16/u64 compact loop has four input loads/shifts, four
qword-to-word register narrows, three joins, one word shift, and two 32-byte
memory narrows for 32 values. The separate passes issue 64 eight-byte memory
narrows per 256 values; compact32 issues 16 thirty-two-byte stores. The joint tile
retains 64 stores while reading the source once. These are generated-instruction
counts, not a CPU throughput model. All inspected primary hot loops have no
calls or stack accesses. Function bodies still contain partial-boundary code
and its ordinary memset calls; whole-operation wrappers call a shared payload
function once.

Compiler grain is materially different across arms. For full-feature AVX512
K17/H16/u64, the separate head loop expands 256 values, the natural joint loop
expands 64, and compact works on 32. The explicit full-tile arm exposes that
variable; it establishes no production unroll policy. Under AVX2-only, forcing
a full tile grows the head function from 1214 to 5067 bytes. Exact sizes and first
backward-loop records are in `local-checks.json`; `audit.py` recreates assembly
and detailed records from a binary.

## Prior comparison correction

The retained Calico `planes.h` hash is
`19272a6e7999ad121512ae84c1a55ea62a53466a9d656d71224f53618beadde5`.
Its Shape(K17/K23) emits **two leading 8-bit planes**, matching SeriesPack's
head representation. The earlier claim that this prior used one 16-bit head
plane was incorrect. `prior_audit.cpp` calls that pinned native plane compiler
without rewriting its algorithms, with the same header-inline annotation and
adapter-only unroll flags as the main comparator. Its full-feature K17/K23
assembly also uses 64 qword-to-byte memory narrowing stores per 256 values.
Thus neither a presumed 16-bit head plane nor head-store count alone explains
the original whole-operation gap.

Calico completes heads and payload per 256-value cell. Series currently encodes
the entire payload array before whole-array head passes. The geometry and
wider bodies can also differ: at K56 Calico has the leading 8+8 planes plus
8- and 32-bit body planes; Series has a 40-bit local AoS body. The prior remains
an additional whole-operation comparison. This diagnostic holds the current
Series payload scheduling common across arms and does not claim to exhaust
the original whole-operation gap.

## Validation

`check.cpp` constructs expected head bytes independently. It covers H8/H16,
8- and 256-value head packets, u8/u16/u32/u64 source carriers, nonzero high input
bits, partial counts 0..257, independently strided planes, guard-page source
and destination boundaries, zero-count inaccessible pointers, canonical final
slack, and modulo128 starts. Inactive H8 head1 is inaccessible. The ordinary
optimized checker must pass on each real hardware profile before any exact-
access acceptance.

The AVX2 timing fixture passes 288 checks under QEMU. A supplemental checker
compiled with `-fno-vectorize -fno-slp-vectorize` passes 145,152 cases there; its
explicit native kernels remain enabled. This is supplemental, not acceptance
of the ordinary optimized binary. Clang can vectorize a baseline partial loop
into `vpmaskmovd`. QEMU faults on its inactive guard-page lanes, and the tiny
`masked_load_check.cpp` reproduces the behavior independently with one active
readable lane and seven inactive inaccessible lanes. Retain and run that
reproducer on actual hardware alongside the **unchanged normal optimized
checker**, to distinguish emulator behavior from any baseline assumption.
Normal AVX2 and full-feature checkers compile successfully; their hardware
execution is pending.

## Running

Use the pinned Linux compiler and run cases sequentially on a pinned CPU.
From the repository root (prefix commands with `orb -m ubuntu` through the
macOS mount):

```sh
probe=workbench/spikes/seriespack-head-projection/encoder
out=build/seriespack-head-encode
mkdir -p "$out"
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=x86-64-v3 -Iikea/include "$probe/check.cpp" -o "$out/check-avx2"
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=x86-64-v3 -Iikea/include "$probe/bench.cpp" -o "$out/bench-avx2"
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=x86-64-v3 "$probe/masked_load_check.cpp" -o "$out/masked-load"
"$out/masked-load"
"$out/check-avx2"
"$out/bench-avx2" --check-only
SIXDB_CPU=0 "$out/bench-avx2" --focus > "$out/samples-avx2.jsonl"
python3 "$probe/audit.py" "$out/bench-avx2" "$out/codegen-avx2.json"
```

Use the actual worker's full feature profile and target tune for its second
build. The local cross compile uses `--target=x86_64-linux-gnu
--gcc-toolchain=/usr`; its full-feature audit uses `-march=znver5`. Do not apply
the supplemental vectorization flags to hardware validation or timings.

The focus contains K17/H16/striped/u64, K23/H16/striped/u64 and
K56/H16/local/u64, each with heads-only and with-payload scopes and
256/8192/65536 values. Omitting `--focus` adds local residuals, byte-aligned
K24/H16 and H8, and narrower input controls. Full fixtures have 288 AVX2 or 576
full-feature checks; focused fixtures have 72 or 144. Timing records 16 balanced
order rotations with serial case execution: focused runs produce 1152/2304
rows; full runs 4608/9216. The outer loop uses one opaque array call per pass,
calibrated to at least 20 ms for the separate AVX2 arm. `SIXDB_CPU` selects an
allowed CPU, which is pinned and reported. Input, payload, head0 and head1
addresses are shared across arms of a matching shape and their page offsets
are recorded. Cache residency is unestablished.

Compare matching host, feature ceiling, shape, carrier, count, and scope.
The normal joint tile and explicit unroll control distinguish input reuse
from compiler grain. A positive result must survive both Zen5 and GNR and
include whole-operation improvement before production selection.
