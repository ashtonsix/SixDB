# Admitted materializing regions

This diagnostic separates three questions left by the coherent Release access
run: the cost of the physical reader and unsigned 64-bit bridge, the cost of runtime
placement, and the cost of the fully dynamic range endpoint. It changes no
compiled production range driver. Its original hardware captures predate the
subsequent expression-consuming materialize operation.

The benchmark-local binder in
[`seriespack_range_regions.h`](../regions.h) supplies two
callbacks. `read` admits count 16, alignment 16, and unsigned 64-bit output at setup,
while retaining the actual source's runtime base and stride. `raw_dense` admits
dense placement too and has the immediate predecessor's pointer/index/output
ABI. Both keep the original index dynamic. The benchmark driver must preserve
one indirect call per query, the same indices, output, and checksum. These are
alternatives to measure alongside the bound endpoint and primary predecessor,
not replacements selected from instruction counts.

The diagnostic's `materialize_region<...,16,16>` now consumes an expression;
physical-view wrappers call `describe` and delegate. It uses the ordinary
`composition::materialize` operation and sinks retaining the previous native
expansion/store bodies. This removes the diagnostic's internal source
description without promoting its count/alignment scope into a public endpoint.
The general operation also accepts substituted expressions and projected heads;
its result domain and selection reach the sink unchanged.

Local56 uses the existing native `composition_ops` through `composition::read`
in a fixed two-tile driver. Narrow local/striped sources produce sixteen byte
lanes independently of the unsigned 64-bit sink width. The striped executor resolves
the actual child and original tile/lane before calling the production private
`range_regions::striped<W,16>` helper. Wider striped bodies resolve their own
child and join in registers using the production body/expand helpers. Local
stride gaps select two exact eight-value reads; they never borrow a gap.

The header states the admission and lifetime requirements. This experiment
covers headless Local 1..7/56 and all selected striped widths, not a proposed
release support matrix or an unrestricted native-fragment contract.

## Release diagnosis

The inspected V2 executable is the coherent run
`20260910T174905Z-4b181dfd`. Its remaining get16 medians include striped3
7.079 ns (1.575 times its same-wire predecessor), striped5 8.207 ns (1.412),
striped6 7.525 ns (1.318), striped7 7.361 ns (1.453), Local1 4.930 ns (1.217),
and Local56 4.866 ns (1.207). These timings motivated diagnosis; the local
assembly below is not a performance result.

The actual V2 caller executes 21 instructions per bound query versus 17 for the
predecessor. Both make one indirect call and share the stack checksum update.
The bound path additionally stores its output descriptor, reloads the bound
function pointer, and forms the range end. The large bound-reader copy occurs
before `StartKeepRunning`, so it is not charged per query.

The striped5 unsigned 64-bit entry then executes 24 instructions selecting the output
carrier and same-tile range path. Its boundary receives five scalar registers
(payload base, stride, begin, end, output): Release argument promotion removed
the source-level placement reference. There is no placement copy or placement
metadata reload inside that boundary. It still performs loop, edge, and
remainder bookkeeping for a sixteen-value query. The one/two-field paths total
110/113 instructions including the caller, versus68/69 for the direct prior.
Both physically load the same one/two 16-byte fields, then perform eight TBL
widening shuffles and four paired output stores.

Local56 likewise loads the same eight exact windows and performs eight TBLs
and four paired stores. The dynamic path executes two iterations of its
complete-tile loop. Its caller plus entry/loop totals 78 instructions (including
two executed NOPs), versus 48 for the prior's expanded two-tile region.

The benchmark-local NEON compilation removes that dynamic work without
changing the leaves. Local56's admitted/raw callbacks have 31/28 instructions,
versus 31 in the actual prior callback. Striped5's raw callback has 50/52 executed
instructions for one/two-field classes versus 51/52 in the prior; runtime
placement adds one instruction in its admitted callback. No callback uses a
stack frame or calls an internal helper in the inspected NEON, AVX2, or
AVX512-profile objects. Source references and inline native arrays disappear;
no byte scratch mediates widening.

There is still a distinct leaf question for Local1. The prior uses two LD1R
byte loads and combines their halves. The production complete-loop pair uses
a halfword load, scalar-to-vector insertion, and TBL replication. In the
admitted/raw context the same production helper lowers to a direct SIMD
halfword load plus replication TBL, eliminating the scalar transfer. That
context-sensitive lowering should be measured separately from generic range
cost. These observations do not classify any remaining ratio as acceptable.

## Checks and reproduction

[`check.cpp`](check.cpp) exercises 20 descriptions, 400 protected placements,
and 6,744 original-coordinate queries per target. Expected values are generated
independently; construction uses the separately wire-verified scalar encoder.
It covers both guarded ends, read-only sources, protected inter-tile gaps,
exact output plus unselected canaries, partial logical lengths, every aligned
position in the fixtures, nonzero tile ordinals, and both callbacks where
dense admission holds. NEON Release, NEON ASan/UBSan, and AVX2 under QEMU pass.
The full AVX512 profile compiles and links; execution awaits native hardware.

From the repository root, prefix Linux commands with `orb -m ubuntu` on macOS:

```sh
clang++-21 -std=c++23 -O3 -g -march=armv8-a -mtune=neoverse-v2 \
  -Iikea/include \
  workbench/spikes/seriespack-range-execution/regions.cpp \
  workbench/spikes/seriespack-range-execution/prototype/check.cpp \
  -o build/seriespack-v2-range-followup/check-neon
build/seriespack-v2-range-followup/check-neon --neon
```

Use `-fsanitize=address,undefined -fno-omit-frame-pointer` for the sanitizer
check. Cross AVX2 uses `--target=x86_64-linux-gnu --gcc-toolchain=/usr -mavx2`
and `qemu-x86_64 -cpu max -L /usr/x86_64-linux-gnu ... --avx2`. The full profile
uses `-mavx512bw -mavx512vbmi -mavx512vl` and `--avx512` on capable hardware.
`receipt.json` retains source/context hashes, exact path counts and assembly
summaries; bulky objects, disassembly and local receipts are under
`build/seriespack-v2-range-followup/`. No new timing run is claimed here.
