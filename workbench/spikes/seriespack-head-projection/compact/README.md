# H16/u64 compact projection in public encoders

This isolated candidate follows the [shared-head hardware experiment](../encoder/hardware.md).
That experiment supports compact16 on AVX2 and compact32 on AVX512 across both
hosts and three extents. It does not by itself establish acceptance inside the
public encoder. Production files are unchanged.

The candidate applies the same mechanism to every legal H16/u64 description:
49 Local widths K16..64 and 12 striped widths. Source qwords are shifted by
payload width before narrowing to consecutive 16-bit words; the high and low
bytes then go to the independent head0/head1 planes. Projection is an inline
register operation. A separate non-suspending traversal receives source,
length, two byte pointers and two strides as scalar arguments. The existing
payload pass, H8 behavior, other input carriers and decoding remain unchanged
in source. Compiler inlining changes at shared encoder entries are audited and
require actual public controls.

Adjacent head tiles form one region only when both strides equal the logical
tile width. Strided heads retain independent traversal. Complete regions use
16/32-value projections; remaining native groups and at most 3/7 conditional
scalar values read exactly n source values. Final head slack is zeroed, and
stride gaps remain untouched. The scalar bound is expressed directly to avoid
unreachable auto-vectorized suffix versions. Strided outer-loop unrolling is
disabled to keep repeated traversal bodies small.

`prepare_overlay.py` requires coherent native.cpp
`1abf641b246af9c075e8234a9e1b3390c412a8410af9353d84d5c52cd57350da`
and emits a new native.cpp, a patch and a receipt. It never edits its source.
`projection.inc` is inserted into that candidate, not included by production.
The NEON all-head, Local2, Scan4 and x86 Local1/Local4 candidates are separate.

`check.cpp` uses the independent public wire oracle, including metadata effects,
plus exact guard-page inputs and all three output planes. It checks all 61
admitted descriptions and four unchanged H8/headless controls, every source
carrier, n=0..33 and boundaries around 64/128/256, both guard ends, and four
independent head-stride combinations. Expected payload, head, slack and gap
bytes come from the specification oracle rather than native decode. Normal
optimized AVX2 and AVX512 builds must pass on real hardware. Local QEMU results
are supplemental.

The public comparison uses 77 cases per profile: all 61 affected H16/u64
encodes, four narrower-carrier encodes, six H8 encodes, two headless encodes,
and four decode controls. It uses the unchanged Google Benchmark fixture at
256, 8192 and 65536 values, five sequential .05-second repetitions in ABBA
process order. There are 4,620 iteration rows per profile. This compares whole
bound public encoders; it neither models core instruction cost nor establishes
cache residency.

Run the captured wrapper in a retained coherent worker workspace:


Commands below using retired campaign wrappers are historical. Recover those
wrappers and their captured inputs through the [archive guide](../../ikea-composition/archive/validation-20260911.md).

```sh
workbench/spikes/ikea-composition/validation/head16-compact-cloud.sh zen5
# or granite-rapids; an optional second argument selects avx2 or avx512
```

The driver checks the old library hashes, recompiles only candidate native.cpp,
and links that object before the unchanged archive. It reuses the retained
public benchmark, prior, and integration-test objects. No before library or
whole matrix is rebuilt. It records source and link-input hashes, compiler
commands, peak compile RSS, binary hashes, sizes, complete disassembly, all
raw repetitions and normal public/guard checks. The source manifest must match
before and after the run. Worker dispatch remains with the parent task.

The adjacent replay driver expects the original captured checkout and its source
paths. Moving this study does not change those measured identities; use the
[archive recovery guide](../../ikea-composition/archive/validation-20260911.md) to restore them.
