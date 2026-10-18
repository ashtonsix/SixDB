# Native early-exit code audit

These notes refer to the qualifying jobs listed in
[the results](../../EARLY-RESULTS.md#correctness-generated-code-and-retained-evidence).
The adjacent JSON reports contain the SHA-256 of each full demangled disassembly.
Recover that member from the corresponding export's `artifact.json` and rerun
`../../audit.py INPUT OUTPUT` to reproduce a report. Full disassembly remains in
the verified worker bundles, rather than duplicating gigabytes here.

All five main profiles contain 950 `early::measure` instantiations. Each x86 ZMM
follow-up has 95; each zero-test follow-up has 24. None has unexpected outlined
calls. `strlen`/`bcmp`/`memcmp` option-selection calls precede warmup and the clock;
the clock and printing calls remain outside the measured loops. The audit allows
those setup calls and does not by itself prove their placement, bounded loads,
or result-dependent addressing. The selected control-flow inspection below
provides the complementary check. Eight-stream code has stack traffic: inlining
does not mean register allocation is free.

## Head8 K64 equality, guarded production suffix

The actual measured function is the `early::measure` instantiation whose lambda
belongs to `early::run_variant<64u, 0, 8u, 3>`. Instruction bytes and long symbol
suffixes are omitted from these excerpts; addresses identify the originals.

Zen 5 AVX2, `avx2-disassembly.txt`, dependent loop. Its start clock is at
`0x577573`; the suffix block starts at `0x577610`.

```asm
5776fa: vpcmpeqb   (%r8,%rcx), %xmm0, %xmm0
577700: vpmovmskb  %xmm0, %ecx
577704: testl      %ecx, %ecx
577706: jne        0x577610       # nonzero head mask: read/compare suffix
57770c: xorl       %ecx, %ecx
57770e: jmp        0x5776b5       # zero result: address recurrence
```

Zen 5 AVX-512, `avx512-disassembly.txt`, corresponding dependent loop:

```asm
4e0017: vpcmpeqb   (%r8,%rcx), %xmm0, %k0
4e001e: kortestw   %k0, %k0
4e0022: jne        0x4dff30       # suffix block
4e0028: xorl       %ecx, %ecx
4e002a: jmp        0x4dffd6       # address recurrence
```

The compiler retains the comparison in a mask register and tests it directly;
the explicit zero-test follow-up therefore does not introduce a new fast path
on AVX-512. Granite's corresponding dependent test is `kortestw` at `0x4dec1b`.
The Zen suffix implementation also uses masked ZMM byte loads and byte
permutation; “AVX-512” here is not a claim that every stage is the same width.

V2 NEON, `neon-disassembly.txt`, start clock `0x546da4`. The dependent head test
materializes the two halves of a sixteen-bit lane mask:

```asm
546f60: cmeq   v0.16b, v0.16b, v1.16b
546f64: and    v0.16b, v0.16b, v17.16b
546f68: addv   b1, v0.8b
546f6c: ext    v0.16b, v0.16b, v0.16b, #8
546f70: addv   b0, v0.8b
546f74: fmov   w10, s1
546f78: fmov   w13, s0
546f7c: bfi    w10, w13, #8, #8
546f80: cbnz   w10, 0x546de8      // suffix block; first payload load 0x546e00
546f84: mov    x10, xzr
546f88: b      0x546f24           // zero-result recurrence
```

At the nonzero-result join, `0x546f20` multiplies the complete result mask by
`0x9e3779b97f4a7c15`. The cursor plus salt is separately multiplied at `0x546f30`,
XORed at `0x546f34`, and shifted by 56 at `0x546f38`. That index selects the next
query at `0x546f50` and head at `0x546f54`. Thus upper result bits contribute to
the next address; this is not a reduction that accidentally keeps only lane zero.
The zero-result edge can skip the result multiplication because its input is
known zero. The independent loop repeats the same relation with eight distinct
stream salts, including on zero results.

## NEON vector-zero follow-up

`early-zero-neon`'s `neon-disassembly.txt`, `early::measure` for the lambda in
`zero_case<64u, 0>`. Start clock is `0xe444`; the dependent test is:

```asm
e61c: cmeq   v0.16b, v0.16b, v1.16b
e620: umaxv  b1, v0.16b
e624: fmov   w12, s1
e628: cbnz   w12, 0xe488          // materialize mask and compare suffix
e62c: mov    x10, xzr
e630: b      0xe5e0              // zero-result recurrence
```

The mask conversion is deferred to the surviving path. This explains the
throughput opportunity measured on NEON, while adding a reduction for surviving
windows. The x86 follow-up emits the requested `vptest` for AVX2; measurements
show that it does not beat the simpler movemask/test path in these cases.

## Full ZMM control

`zmm-disassembly.txt`, measured lambda in
`wide_variant<64u, 0, 0u, false>`. The independent Zen loop contains, for example:

```asm
15428: vpbroadcastq (%rdi,%r10,8), %zmm0
1542f: vpcmpeqq     (%rsi,%r8), %zmm0, %k0
15436: vpcmpeqq     0x40(%rsi,%r8), %zmm0, %k1
15442: kunpckbw     %k0, %k1, %k0
15446: kmovw        %k0, %r8d
1544a: imulq        %r12, %r8
15454: xorq         %r9, %r8
```

Two full-width comparisons cover sixteen 64-bit values and form the complete
mask before address mixing. Granite emits the same pair of ZMM comparisons,
including at `0x14d29` and `0x14d38`. There is no unpacking/reconstruction seam
in this independent control. Its throughput advantage over a guarded byte
extraction is included in the recommendation.

Branch prediction is not modeled by LLVM-MCA here. Native timings and paired
hardware branch-miss/cycle counters support the stopping-policy conclusion;
a straight-line scheduling model would not establish the mixed-window cost.
