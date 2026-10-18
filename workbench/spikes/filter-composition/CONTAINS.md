# Short Local bitplane contains tests

GFNI provides a four-instruction prepared test including the load. Ordinary
AArch64 provides a five-instruction test for five through seven planes, or four
for four planes, by reducing matches directly. Both counts end in flags;
materializing a Boolean and preparing a runtime query cost extra. These are
eight-row bitplane tests, not complete mixed-width key lookups.

## AVX-512 + GFNI

The prepared AVX-512 + GFNI core takes four instructions **including the data
load**, ending with condition flags. Returning a materialized C++ bool adds
`setne`; returning a rank needs mask transfer and first-set-bit extraction.
This is a bitplane/residual operation, not a four-instruction mixed-width key
lookup. [The source](contains.cpp) separates the prepared core from a complete
runtime-query example.

```asm
vgf2p8affineqb xmm0, xmm0, qword ptr [rdi]{1to2}, 0
vpxor         xmm0, xmm1, xmm0
vptestnmb     k0, xmm0, xmm2
kortestw      k0, k0
```

`xmm0` initially holds repeated `0x8040201008040201`, `xmm1` holds the repeated
bit-reversed query byte, and `xmm2` holds the repeated significant-bit mask.
The first instruction broadcasts an eight-byte matrix from memory and transposes
its bits into eight bytes, twice. The XOR and byte-wise masked zero test compare
all eight values; repeated upper lanes do not change the contains verdict.
Intel documents the underlying [8×8 affine transform](https://builders.intel.com/docs/networkbuilders/galois-field-new-instructions-gfni-technology-guide.pdf).
The instruction sequence above is compiled from our source, not quoted from Intel.

For R Local planes, bit b of row j is bit j of byte b. With this matrix ordering,
the transform puts that bit in output-byte position 7-b. Define
`Q = bit_reverse8(query)` and `S = (0xff << (8-R)) & 0xff`. Row j matches exactly
when `((transformed[j] ^ Q) & S) == 0`. Bytes R through7 can contain arbitrary
owner data: their output bits fall outside S. For R=6, S is `0xfc`.

The obligations and costs matter:

- The query is in the R-bit domain, or a preceding range test rejects it.
- Eight bytes must be readable. A generic six-byte leaf does not grant that
  proof. In the B64/K30 Iceberg–Swiss layout, residual starts24 and54 admit
  reads through31 and61 respectively inside the initialized owner bucket.
- Basis and S are reusable format constants. Query broadcast/reversal are real
  per-query work; multiple tiles can share them. The runtime example and the
  complete index consumer charge them.
- A 30-bit value also has a 24-bit body. Its comparison and candidate masking
  are additional work. The promising composition compares bodies first and
  invokes this residual test only for candidate groups.
- The mask-producing form can intersect candidates and supply `indexof`.
  A contains-only instruction count does not establish lookup latency or
  reciprocal throughput.

This does not change SeriesPack's bounds or stored format. The arithmetic now
lives in Ikea's `composition::local_equal8<R>`; the [x86 qualification](X86-INDEXOF.md)
measures bounded and owner-wide loads through complete consumers and qualifies
the public `indexof` lowering. Baseline AVX2 uses explicit body SIMD plus a
bit-parallel scalar residual reduction, with fast BMI2 preparation on the two
measured tuning targets. This is not an analogous general four-instruction
AVX2 test. Ordinary word-width tiles retain their native equality paths.

The pinned compiler's prepared witness is four instructions plus `setne` and
`ret`. Native randomized correctness passed on Zen5 and Granite. The [retained native
disassembly](evidence/tiles/zen5-contains-codegen.txt) includes the longer runtime-
query function. [Matched consumer results](TILE-FILTERS.md#first-match-decision)
charge query preparation and the body comparison, with wins and losses retained. The LLVM-MCA experiment
is only a scheduling diagnostic: its load model and artificial repeat of the
ABI-clobbered basis register must not be reported as a measured lookup time.

## AArch64: reduce the planes directly

The [ARM prototype](contains-arm.cpp) compiles to this six-plane prepared core:

```asm
ldr  x8, [x0]
eon  x8, x1, x8
and  x8, x8, x8, lsr #16
and  w8, w8, w8, lsr #16
tst  w8, w8, lsr #8
```

`x1` contains the prepared query: byte b is `0xff` when query bit b is one,
otherwise zero. `EON` is XOR-with-NOT, making bit j of byte b one exactly when
row j matches query bit b. ARM's shifted logical operands combine the shift
and AND in one instruction.

The first AND combines planes 0/2, 1/3, 2/4 and 3/5. The **32-bit** second AND
combines those into two bytes and zeros everything above bit15. The final TST
intersects the two bytes, setting NE precisely when at least one row matches
all six planes. The two extra loaded bytes cannot influence the result.
This narrowing is essential: keeping that second AND 64-bit would leave
unrelated high-byte matches capable of producing a false positive.

For R=5..8, the first shift is `8*(R-4)`; the other steps stay the same. For
R=4, omit the first AND and use a four-byte load, giving **four instructions
including the load**. R=8 is an algebraic control; ordinary eight-bit SeriesPack
uses byte values, not these eight bitplanes. A materialized Boolean adds `cset`.
Replacing TST with AND instead returns the exact eight-bit row mask, which can
feed candidate intersection and first-set-bit extraction for `indexof`.

No NEON or SVE extension is required for this prepared core. The runtime example
uses NEON `dup; cmtst; fmov` to expand the query, plus a reusable bit-weight
constant and a domain check. An inlined caller may share this preparation across
tiles. The five-instruction count requires eight readable bytes for R>4, just
as the GFNI control does; it does not authorize an eight-byte read of a standalone
six-byte allocation. A K30 lookup still needs its 24-bit body comparison.

Pinned Clang21.1.8 initially split the first shifted AND into LSR+AND and rewrote
the four-plane EON reduction into extra operations. Empty register constraints
in the prototype preserve the intended dataflow without adding instructions.
The [retained assembly](evidence/tiles/arm-contains-codegen.txt) includes both
prepared witnesses, the exact-mask form and the complete runtime-query function.

Native correctness passed on the local Apple AArch64 Linux VM: R4..8, every
in-domain query for 2048 random unaligned tiles per width, with arbitrary bytes
after R, compared with independent row-by-row decoding. ASan/UBSan also passed.
This establishes the algorithm and code generation, **not Neoverse latency or
reciprocal throughput**; no new consumer timing is claimed here.

An additional SVE2 BitPerm control uses three BGRP instructions to
transpose an 8×8 bit matrix, followed by masking and byte comparison. It requires
the optional BitPerm extension. The follow-up V2 job `20261015T232934Z-b5b62df5`
passed its native correctness check, but did not time or select it.
The direct scalar reduction is already shorter for this contains operation.
The [ARM follow-up](ARM-INDEXOF.md) owns charged preparation, complete consumers,
graduation and the measured latency/throughput tradeoffs.
