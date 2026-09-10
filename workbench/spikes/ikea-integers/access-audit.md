# Native read footprint audit

[Back to the spike](README.md). This page audits architectural payload bytes
read by emitted instructions, including active mask elements and folded memory
operands. It does not measure cache transactions, prefetching or speculation.
Each section identifies its captured source; earlier tables are not silently
relabelled as later code.

| Current question | Captured audit |
| --- | --- |
| Continuous Scan5/7, arithmetic reader | [Exact fragment sets](#continuous-scanpack57) |
| LocalPack get16 refinements | [Exact loads by target](#localpack-get16-changes-in-the-same-captures); unchanged cases use the historical table |
| Scan6, arithmetic reader | [One-branch read](#follow-up-measured-scanpack6-reader-grouped-by-fragment-count) |
| Continuous Scan5/7, constant-offset reader | [Payload versus dispatch-table accesses](#follow-up-measured-constant-offset-scanpack57-readers) |
| 12-bit composition | [Combined body/tail accesses](#composition-body-and-tail-together) in the original composition capture |
| Width-56 bodies | [Separate wider-body audit](wide56/README.md#data-and-native-work) |

Both selected narrow wires satisfy the point and aligned-get16 bounds at every
reachable phase. The [stored-bit proof](locality/README.md) and [functional checks](bench.md#validation)
provide complementary evidence; neither substitutes for this instruction audit.

## Follow-up: measured continuous ScanPack5/7 and LocalPack get16

This follow-up audits the continuous 5/7 candidate and LocalPack get16
endpoints in the following hardware captures. The historical second-source
and composition audit below retains its original scope. These are the
arithmetic readers; the constant-offset dispatch captures have a separate
audit below.

The worker prefix is `20260909T192105Z-`. Each linked bundle retains the
run's disassembly and source archive.

| Target | Worker | Width 5 run | Width 7 run |
| --- | --- | --- | --- |
| Zen 5 | `d3288c40` | [20260909T192222.328837Z-zen5](evidence/continuous-zen5-k5/artifact.json) | [20260909T192245.744304Z-zen5](evidence/continuous-zen5-k7/artifact.json) |
| Granite Rapids | `89e46e3f` | [20260909T192226.908615Z-granite-rapids](evidence/continuous-granite-rapids-k5/artifact.json) | [20260909T192254.275510Z-granite-rapids](evidence/continuous-granite-rapids-k7/artifact.json) |
| Neoverse V2 | `61a83146` | [20260909T192238.475343Z-neoverse-v2](evidence/continuous-neoverse-v2-k5/artifact.json) | [20260909T192307.373526Z-neoverse-v2](evidence/continuous-neoverse-v2-k7/artifact.json) |

All six source archives agree on `formats.h`, `local.h`, `scan.h`, and
`codec.cpp`: SHA-256 prefixes `ffbb010e6083`, `2837915d5375`,
`ad7aa720333a`, and `2cc3edbe7551`. The compiler and target settings remain
Linux Clang 21.1.8 with the settings listed in the historical audit.
The endpoints convey `i < 256`, and get16 also conveys `i % 16 == 0`;
neither adds a payload-alignment assumption.

### Continuous ScanPack5/7

The emitted point reader performs one byte load before its split branch
and a second byte load only for a crossing value. The second address is
exactly 32 bytes after the first. Get16 has the same branch-local access
sets with 16-byte loads. On x86 its second load is a folded XMM memory
operand of `vpternlogd`; on V2 both are `ldr q` instructions. Mask
broadcasts and result stores are separate from payload reads.

| Width | Chunk sets in logical group order |
| --- | --- |
| 5 | `{0}`, `{0,1}`, `{1}`, `{1,2}`, `{2,3}`, `{3}`, `{3,4}`, `{4}` |
| 7 | `{0}`, `{0,1}`, `{1,2}`, `{2,3}`, `{3,4}`, `{4,5}`, `{5,6}`, `{6}` |

For lane `l`, a point reads exactly `32*c+l` for each listed chunk `c`;
get16 reads `[32*c+l,32*c+l+16)` with `l` equal to 0 or 16. These stay
within the 160-byte and 224-byte payloads. Every pair uses adjacent chunks,
so point and get16 envelopes are at most 33 and 48 bytes. At primitive
phases 0 and 32 from a 64-byte-aligned array, their last byte is at most
95 relative to the first cache line: both satisfy the two adjacent-line
bound. The seven-bit representation has more two-load groups and no
three-load group; its mean is still 1.75 payload bytes per point.

Enumerating both phases gives 512 point cases and 32 get16 cases per
width. Width 5 crosses a line in 128 and 8 cases respectively (25%);
width 7 does so in 192 and 12 cases (37.5%). These are logical footprint
counts, not observed cache transactions. All four reader instruction
sequences were also checked in both measured binaries per target; they
agree with the controlled-object audit after relocation normalization.

### LocalPack get16 changes in the same captures

The payload union remains exactly `[a,a+2*k)`, where `a=(i/8)*k`.
The changed instructions reduce load setup without widening that union:

| Width | Zen 5 payload load | Granite Rapids payload load | V2 payload loads |
| --- | --- | --- | --- |
| 1 | `movzwl`, 2 bytes | `vmovw`, 2 bytes | two one-byte `ld1r` loads |
| 2 | `vmovd`, 4 bytes | `vmovd`, 4 bytes | one `ldr s`, 4 bytes |
| 4 | `vmovq`, 8 bytes | `vmovq`, 8 bytes | `ldp s0,s1`, two 4-byte words |

X86 widths 3, 5, 6, and 7 retain masked XMM byte loads with masks
`0x3f`, `0x3ff`, `0xfff`, and `0x3fff`: exactly 6, 10, 12, and 14 active
bytes. V2's other widths retain the overlapping halfword/word accesses
listed in the [historical table](#localpack-no-widened-payload-interval). Its width-2 load replaces four scalar
byte reads; table lookup and BGRP operate on registers after that exact
four-byte load. All seven LocalPack get16 instruction sequences agree
between the two captures for each target. Enumerating all 64 byte phases
for every width gives 448 cases; all satisfy the two adjacent-line bound.

## Follow-up: measured ScanPack6 reader grouped by fragment count

The following `20260909T192509Z-` workers measured the revised width-6
arithmetic reader. This section covers its point and get16 endpoints;
it does not extend the composition audit or cover later reader choices.

| Target | Worker | Run and retained bundle |
| --- | --- | --- |
| Zen 5 | `228d93af` | [20260909T192533.881882Z-zen5](evidence/scan6-zen5/artifact.json) |
| Granite Rapids | `c7ba22ce` | [20260909T192534.336539Z-granite-rapids](evidence/scan6-granite-rapids/artifact.json) |
| Neoverse V2 | `f70c4dce` | [20260909T192533.426148Z-neoverse-v2](evidence/scan6-neoverse-v2/artifact.json) |

These archives share `scan.h` SHA-256 prefix `d22f4854e6d5`; the format,
LocalPack, and endpoint source hashes match the preceding follow-up.
The 128-value primitive remains 96 bytes, so a 256-value endpoint contains
two primitives. Let `b=96*(i/128)+i%32` and `g=(i%128)/32`.

| Group `g` | Point byte offsets | Get16 byte intervals |
| --- | --- | --- |
| 0 | `b` | `[b,b+16)` |
| 1 | `b`, `b+32` | `[b,b+16)`, `[b+32,b+48)` |
| 2 | `b+64`, `b+32` | `[b+64,b+80)`, `[b+32,b+48)` |
| 3 | `b+64` | `[b+64,b+80)` |

All three targets emit the first byte/XMM/Q load before a single split
branch. Only groups 1 and 2 execute the second load, at `b+32`; each
payload instruction reads one byte or exactly 16 bytes. On x86 the
ternary instruction in get16 reads a mask constant, not another payload
fragment. This preserves the prior format's chunk sets
`{0}`, `{0,1}`, `{1,2}`, `{2}` and exact primitive bounds.

At phases 0 and 32, the 256 point cases and 16 get16 cases all satisfy
the two adjacent-line bound, with maximum envelopes of 33 and 48 bytes.
Exactly 64 point cases and 4 get16 cases cross a line (25%). Each of the
nine hardware runs in these two follow-ups also retains passing checks
for 14,336 codec cases and 3,670,016 point reads. Those functional and
guard-page checks complement this instruction-level footprint audit.

## Follow-up: measured constant-offset ScanPack5/7 readers

These captures exercise `ScanReader::constant_offsets` on the same
continuous wire. Both reader choices remain available; this audit concerns
their architectural payload access sets, not their relative performance.

| Target | Worker job | Width 5 run | Width 7 run |
| --- | --- | --- | --- |
| Zen 5 | `20260909T193008Z-35de0c38` | [20260909T193030.383643Z-zen5-constant-offsets](evidence/dispatch-zen5-k5/artifact.json) | [20260909T193052.622641Z-zen5-constant-offsets](evidence/dispatch-zen5-k7/artifact.json) |
| Granite Rapids | `20260909T193007Z-f4fb9abf` | [20260909T193031.783803Z-granite-rapids-constant-offsets](evidence/dispatch-granite-rapids-k5/artifact.json) | [20260909T193058.471453Z-granite-rapids-constant-offsets](evidence/dispatch-granite-rapids-k7/artifact.json) |
| Neoverse V2 | `20260909T193007Z-fcfec837` | [20260909T193033.201604Z-neoverse-v2-constant-offsets](evidence/dispatch-neoverse-v2-k5/artifact.json) | [20260909T193101.467131Z-neoverse-v2-constant-offsets](evidence/dispatch-neoverse-v2-k7/artifact.json) |

All six archives retain the preceding `formats.h` and `local.h` hashes.
Their shared `scan.h` and `codec.cpp` SHA-256 prefixes are `3ab5814496fd`
and `309578b6f171`. All four measured 5/7 reader instruction sequences in
both binaries per target match the audited constant-offset candidate
after relocation normalization.

X86 dispatches through an eight-entry jump table. Its 4-byte table-entry
read is control data, separate from the encoded payload. Each selected
point case reads one or two individual bytes at fixed chunk offsets.
Each get16 case reads one or two exact 16-byte payload fragments: XMM
loads and memory-source word shifts. Its ternary-merge memory operand is
a broadcast mask constant, not an additional payload fragment. V2 uses
a balanced branch tree; its selected cases read individual bytes or
`ldr q` payloads. Crossing get16 cases combine two Q loads with USHR and
SLI. Neither target widens a payload read or reads an unused chunk on the
selected architectural path.

Consequently the exact chunk sets, byte intervals, 160/224-byte payload
bounds, and phase enumeration are identical to the continuous 5/7 table
above: at most two adjacent chunks, 33-byte point envelopes and 48-byte
get16 envelopes. The 1,024 point cases and 64 get16 cases across both
widths and both phases retain the two adjacent-line bound. Every listed
dispatch run also passed 14,336 codec cases and 3,670,016 point reads.
This establishes equal payload geometry for two execution choices;
it does not equate their speculative reads or measured cache traffic.

## Historical baseline and composition capture

The second hardware source passes the two adjacent 64-byte cache-line bound
for LocalPack and ScanPack point reads and aligned reads of 16 values, for
widths 1 through 7. All three composition read regions also pass. This audits
the architectural payload bytes read by the emitted instructions, including
active mask elements and folded memory operands. It does not measure cache
transactions, prefetching, or speculative execution.

The inspected Linux Clang 21.1.8 Release builds are under
`build/workers/20260909T181712Z-<worker>/results/<run>/`:

| Target | Worker | Run | Target setting |
| --- | --- | --- | --- |
| Zen 5 | `9b0723ce` | `20260909T181831.708786Z-zen5` | `-march=znver5` |
| Granite Rapids | `9a2e67c0` | `20260909T181829.099988Z-granite-rapids` | `-march=graniterapids` |
| Neoverse V2 | `bfaf49c8` | `20260909T181847.454251Z-neoverse-v2` | `-mcpu=neoverse-v2` |

Evidence is `assembly.txt` and `composition-assembly.txt`; each `run.json`
records their hashes and disassembly commands. The captured source archives
agree on `formats.h`, `local.h`, and `scan.h` (SHA-256 prefixes respectively
`ac7ea308fc0a`, `9204a220568b`, `1fa3d5786a39`). This conclusion applies to
those captures, rather than later kernel edits.
The complete runs are recoverable through the retained
[Zen 5](evidence/composition-zen5/artifact.json),
[Granite Rapids](evidence/composition-granite-rapids/artifact.json) and
[Neoverse V2](evidence/composition-neoverse-v2/artifact.json) bundle references.

## LocalPack: no widened payload interval

For position `i`, let `a = (i / 8) * k`. Point reads cover exactly
`[a, a+k)`; aligned reads of 16 values cover exactly `[a, a+2*k)`.
Overlapping scalar loads do not enlarge these unions.

| Width | x86 point loads, relative to `a` | V2 point loads, relative to `a` | V2 get16 loads, relative to `a` |
| --- | --- | --- | --- |
| 1 | byte at 0 | byte at 0 | bytes at 0, 1 |
| 2 | halfword at 0 | bytes at 0, 1 | bytes at 0, 1, 2, 3 |
| 3 | halfwords at 0, 1 | bytes at 0, 1, 2 | halfwords at 0, 1, 3, 4 |
| 4 | word at 0 | word at 0 | words at 0, 4 |
| 5 | words at 0, 1 | words at 0, 1 | words at 0, 1, 5, 6 |
| 6 | words at 0, 2 | words at 0, 2 | words at 0, 2, 6, 8 |
| 7 | words at 0, 3 | seven predicated bytes | words at 0, 3, 7, 10 |

Here halfword means 2 bytes and word means 4 bytes. V2's `ld1r {v0.8b}`
reads one byte and replicates it; it is not an 8-byte read. Its width-7 point
read uses `ptrue p0.b, vl7` followed by `ld1b {z0.b}, p0/z`, so only seven
byte elements are active.

Both x86 targets use a masked XMM `vmovdqu8` for every LocalPack get16, with
the low `2*k` mask bits enabled. For example, Granite Rapids width 7:

```asm
4c3a: movw       $0x3fff, %ax
4c3e: kmovd      %eax, %k1
4c42: vmovdqu8   (%rdi,%rsi), %xmm0 {%k1} {z}
```

The read is 14 active bytes. Even allowing every possible byte phase,
`63 + 14 - 1 < 128`; this proves the bound for every reachable packed
LocalPack phase. The result store and the shuffle/matrix constants are
separate from the encoded payload.

## ScanPack: branch-local chunk sets

Every captured ScanPack point read loads individual bytes. Every get16
payload access loads 16 bytes: XMM moves or memory-source shifts on x86,
and `ldr q` on V2. No payload access widens to YMM, ZMM, or an unpredicated
SVE load. Branch paths, including paths that share an arithmetic suffix,
must be considered separately.

For each primitive, the following are the chunk sets read by successive
32-position groups. A chunk is 32 bytes; braces denote one branch's set.

| Width | Chunk sets in logical group order |
| --- | --- |
| 1, 2, 4 | `{0}` for every group |
| 3 | `{0}`, `{0}`, `{0,1}`, `{1}`, `{1}`, `{1,2}`, `{2}`, `{2}` |
| 5 | `{0}`, `{0,2}`, `{1}`, `{1,2}`, `{3}`, `{2,3}`, `{4}`, `{2,4}` |
| 6 | `{0}`, `{0,1}`, `{1,2}`, `{2}` |
| 7 | `{0}`, `{0,1}`, `{1,2,3}`, `{2}`, `{6}`, `{5,6}`, `{3,4,5}`, `{4}` |

These match the captured format's bit dependencies. Widths 5 and 7 use the
reordered prior chunks `[0,1,4,2,3]` and `[0,1,2,3,6,5,4]`; the audit does
not assume the earlier sequential-field proposal.

Let `l` be the position modulo 32 and `c` an accessed chunk. Point loads
are `[32*c+l, 32*c+l+1)`; get16 loads are
`[32*c+l, 32*c+l+16)`, with `l` equal to 0 or 16. Primitive strides are
32, 96, 160, or 224 bytes, so both primitive phases 0 and 32 occur from a
64-byte-aligned array. Every branch has `max(c)-min(c) <= 2`.

Consequently the point envelope is at most 65 bytes, whose last byte is
at most `63+64=127` relative to its first line. The get16 envelope is at
most 80 bytes and begins at phase 0, 16, 32, or 48, so its last byte is at
most `48+79=127`. Even ScanPack7's three-load branches occupy at most two
adjacent lines. Contiguity of the bytes between the loads is unnecessary.

## Composition: body and tail together

All three captured `ikea_i12_read_*` regions retain their exact child
footprints. Let `g` be 0, 16, 32, or 48, and `b = (g/8)*12`. Intervals
below are half-open byte offsets within the 96-byte payload.

| Parent placement | Body reads | Tail reads | Envelope lengths for successive groups |
| --- | --- | --- | --- |
| Local packets | `[b,b+8)`, `[b+12,b+20)` | `[b+8,b+12)`, `[b+20,b+24)` | 24, 24, 24, 24 |
| Body64 / tail32 | `[g,g+16)` | `[64+g%32,80+g%32)` | 80, 80, 48, 48 |
| Body32 / tail32 / body32 | `[g+32*(g>=32),g+32*(g>=32)+16)` | `[32+g%32,48+g%32)` | 48, 48, 48, 48 |

On both x86 targets the local body uses two 8-byte `vmovq` loads. The
tails are combined into one masked instruction; the low mask bits are
`0x9`, selecting 32-bit elements 0 and 3:

```asm
# Granite Rapids, ikea_i12_read_local; rax = b
18d19: movb       $0x9, %r10b
18d1c: kmovd      %r10d, %k1
18d21: vmovdqu32  0x8(%rsi,%rax), %xmm1 {%k1} {z}
```

Its active bytes are exactly `[b+8,b+12)` and `[b+20,b+24)`. On V2 the
same regions use 8-byte body loads and 4-byte tail loads. The two Scan4
parents use 16-byte body and tail reads on every target. In particular,
x86 `vpmovzxbw m128, ymm` reads 16 source bytes while producing 32 bytes
of decoded values. The continuation-table load is separate from payload.

The 96-byte parent stride reaches phases 0 and 32. Enumerating both
phases and all four groups proves that the *combined* body and tail
reads occupy at most two adjacent lines for every placement.

The interval check covered 896 LocalPack cases (all 64 phases, both
grains), 2,856 ScanPack cases (both phases, every group and legal lane),
and 24 composition cases. All passed. Existing guard-page and poisoned
child-span checks provide complementary dynamic evidence; guarding only
the whole encoded cell would not prove this internal locality property.
