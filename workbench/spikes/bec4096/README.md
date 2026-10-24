# Bec4096

Bec4096 has **graduated** into Ikea's `ikea::bec` module (renamed from
`ikea::bec256`), beside Bec256: see the [Bec4096 guide](../../../ikea/docs/bec/bec4096.md).
The module adds a portable fallback, prepared encodings and validation;
checks and examples live with it, and the comparison benchmark and storage
tool moved to the [Bec suite](../../benchmarks/bec/README.md#bec4096-against-sixteen-bec256-bodies).
This study keeps the findings, rejected alternatives and retained evidence.

One headless body for 4096 positions, with Bec256's count tree and byte ranks,
one population/length/address per 4096 positions instead of sixteen, and
access to any 256-position block, its byte counts or a rank without decoding
the rest. The question was whether 4096 positions could be faster than sixteen
Bec256 blocks, not merely smaller, by fusing work across blocks and sharing one
kernel core with Bec256.

## Starting point

- [Bec256's representation](../../../ikea/docs/bec/representation.md) supplies
  the law: split fields (left count minus its lower bound, width from the
  parent) and byte ranks. Its native kernels walk one block's five split levels
  root to leaf; the pair decoder (`decode2`) interleaves two such chains.
- The [directory study](../bec256-composition/metadata-findings.md) prices what a
  parent pays for sixteen independent blocks: direct metadata is 4 bytes per
  block; packed directories halve that but add 3–11% to scans and much more
  when terminal blocks dominate. The [whole-bitset study](../bec256-composition/whole-bitsets.md)
  measures the same costs with two operands.
- [Frame phase 1](../frame-phase-1/ENCODING.md#geometry) uses 4096-position groups
  of sixteen 256-position blocks, the geometry adopted here.

What was new to establish: a layout that keeps block access cheap with one
external population/length, and whether whole-group kernels beat sixteen
per-block calls.

## Wire format

All fields are concatenated least-significant bit first, with no gaps;
zero-width fields contribute nothing. Body byte 0 holds stream bits 0–7. The
only padding is zero high bits in the final byte.

1. **Head**: the top four levels of the 4096-position bisection tree (regions
   of 4096, 2048, 1024 and 512 positions), breadth-first, using Bec256's split
   law. It recovers the sixteen block populations. At most 146 bits.
2. **Length table**: for blocks 0–14, the block's stream length in bits, stored
   as `length − min(c)` in `bit_width(max(c) − min(c))` bits, where `min` and
   `max` are the exact extreme Bec256 stream lengths for block population `c`
   (a compile-time DP; the check derives them independently). Populations 0, 1,
   255 and 256 need no bits. Block 15's length is never needed.
3. **Blocks**: each nonterminal block's unchanged Bec256 stream, in order,
   bit-packed without byte padding.

The parent keeps population (13 bits) and byte length (10 bits; at most 784).
A reader decodes the head once into a 64-byte frame (block populations and bit
starts), so block, rank and count access never reread it.

## Shared core with Bec256

Bec256's level step is now one kernel for any stream geometry:
`detail::{neon,avx512}::level<Half, Active, Domain>` in Ikea. Lanes are
stream-major; each stream owns `Domain` lanes of which the first `Active` hold
the level's nodes, with a per-lane byte slot into a 64-byte table and a per-lane
bit cursor. Bec256's single block is one eight-lane domain (`split<Half>` on
NEON), its pair two sixteen-lane domains (`split2<Half>` on AVX-512). Bec4096
packs sixteen streams densely (`Domain == Active`) so its upper levels fill
whole vectors. Bec256's encoder also gained `encode_counts`, taking byte counts
the caller already holds.

Bec256 itself is unchanged in behaviour and cost. `encode_counts` left Bec256's
object code identical on NEON and its instructions identical on AVX-512.
Through `level`, the AVX-512 instruction mix differs by two instructions, and
NEON keeps the bit cursor in a vector lane (fewer general-register moves).
Bec256's routine suite, before/after captures on the same machines (78 cases
each):

| Machine | Geomean after/before | Range |
| --- | ---: | --- |
| Neoverse V2 | 0.988 | 0.93–1.005 |
| Zen 5 | 0.966 | 0.83–1.19; the only cases above 1.04 are 1.1–1.4 ns terminal cases |

## Fused kernels

**Decode.** All sixteen streams advance through block levels A–D together:
16, 32, 64 and 128 nodes in full vectors, from slot tables holding the first
4·Active bytes of each stream's window (fields of the first four levels end by
bit 93). On AVX-512 the tables come from four permutes of the pair trees; on
NEON the 16-byte table of level D is literally four streams' first bytes.
Leaves and ranks then use Bec256's kernels, two blocks per joint register
constraint (Bec256's V2 `decode2` trick, applied on every target). Terminal
blocks need no branch in the fused levels; with fewer than five nonterminal
blocks, decode falls back to per-block calls.

**Counts.** The same walk without rank fields gives all 512 byte populations.

**Encode.** Block counts are computed once and passed to `encode_counts`;
blocks pack independently. The thirty head and length fields are computed in
vectors (each head node's region and left half are differences of an
exclusive prefix of block counts, gathered at fixed lanes) and packed four per
group. On NEON a staging buffer takes word-wise appends
(only each block's first word merges), then one exact copy. On AVX-512 each
block's assembled words are shifted into place in one register, the previous
chunk's last word is carried in a register, and byte-masked stores write the
exact body directly.

## Results

Medians of five repetitions, nanoseconds per 4096 positions (or per access),
pinned CPU, resident inputs rotating over 64 synthetic groups or 128 groups per
RealRoaring family, padded (64-byte readable suffix) grants. Control: sixteen
Bec256 bodies densely packed per group with a resident array of admitted
readers (a generous direct directory) and the same terminal policy. Ratios
below 1 favour Bec4096.

| Operation | V2 random | V2 census | V2 weather | V2 wikileaks | Zen random | Zen census | Zen weather | Zen wikileaks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Full decode, fused | 796 (0.83) | 536 (0.83) | 685 (0.83) | 168 (0.92) | 210 (0.51) | 156 (0.55) | 189 (0.53) | 84 (0.90) |
| One block, retained frame | 89.8 (1.00) | 58.5 (1.03) | 76.1 (1.00) | 14.0 (1.03) | 38.1 (0.96) | 24.4 (0.97) | 32.2 (0.97) | 7.1 (1.06) |
| Rank at a random position | 66.5 (0.70) | 42.8 (0.68) | 52.8 (0.66) | 13.1 (0.83) | 32.6 (0.79) | 21.2 (0.74) | 26.1 (0.74) | 7.6 (0.73) |
| One block's 32 byte counts ÷ one block | 53.0 (0.59) | 33.4 (0.59) | 44.4 (0.59) | 9.5 (0.70) | 26.0 (0.66) | 16.5 (0.66) | 21.9 (0.66) | 5.3 (0.79) |
| All 512 byte counts ÷ fused decode | 452 (0.57) | 326 (0.61) | 395 (0.58) | 151 (0.90) | 92 (0.44) | 78 (0.50) | 86 (0.46) | 61 (0.72) |
| Checked exact encode | 528 (0.96) | 310 (0.93) | 410 (0.94) | 120 (1.08) | 258 (1.06) | 163 (1.09) | 214 (1.08) | 65 (1.31) |
| Fresh block (bind + block) ÷ one block | 146 (1.62) | 117 (2.05) | 133 (1.75) | 74 (5.44) | 96 (2.42) | 76 (3.02) | 87 (2.61) | 44 (6.56) |

Ratios against the control except where a row names another Bec4096 case.
The synthetic shapes agree: fused decode on runs, mixed and sparse groups is
0.82 on V2 and 0.52–0.62 on Zen. Groups with three live blocks (`clustered`)
take the per-block fallback and stay at parity (0.98 / 1.02). Decoded block by block,
the same format is at parity with the control (0.95–1.03), so the gain is the
fused walk, not the layout. Encode on
terminal-heavy groups still pays a fixed head and emission cost: clustered
1.12 on V2 and 1.34 on Zen.

Exact (body-only) grants cost fused decode 1–20% more than padded grants
(worst on `runs`, Zen), because streams near the end of the body take a
zero-padded scratch window.

## Storage

Encoded bytes per 4096 positions, including parent metadata (Bec256: packed
population-code 8 + length 6 per block plus a 4-byte checkpoint, or direct 4
bytes per block; Bec4096: packed 23 bits, or an 8-byte word):

| Input | Terminal blocks | 16×Bec256 + packed | 16×Bec256 + direct | Bec4096 + packed | Bec4096 + word | Length table |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| census-income | 35.9% | 151.3 | 183.3 | 132.8 | 137.8 | 6.6 |
| weather_sept_85 | 15.9% | 132.3 | 164.3 | 114.9 | 119.9 | 7.5 |
| wikileaks-noquotes | 86.5% | 41.0 | 73.0 | 15.2 | 20.2 | 1.3 |
| All 14 RealRoaring families | 76.9% | 56.3 | 88.3 | 30.7 | 35.7 | 1.8 |
| Random | 0% | 657.4 | 689.4 | 655.4 | 660.4 | 16.9 |
| Worst case | 0% | 784.0 | 816.0 | 787.0 | 792.0 | 16.9 |

`bec4096_sizes` prints all families. Bec4096 bodies alone are slightly larger
than sixteen Bec256 bodies on dense data (head plus length table against byte
padding), but much smaller once metadata is counted.

## Layout: why block-sequential with a length table

A whole-tree breadth-first body (Bec256's law extended to 4096, no length
table) would save only the length table: 1.8 bytes per group over the natural
windows (6% of 30.7) and 17 bytes on random data (2.6%). It would cost block
access: locating a block's fields at any level needs every field to its left
at that level, and its ranks need every byte population before it, so a single
block or rank requires the whole count tree. The fused tree-only walk
(`counts_all`) measures that floor: on V2 452 ns against 90 ns for one block
from a retained frame (5×), on Zen 92 against 38 ns. Block-sequential keeps
block access at Bec256's cost and loses nothing in full decode, since the fused
walk takes stream starts from the frame. The length table therefore stays.

## Negative results and open points

- **Vector head decode** (NEON `tbl4` with 32-bit field windows, AVX-512 with
  LZCNT widths): binding cost 69 ns on V2 and 37 ns on Zen against 55 and 33 for
  the scalar head. Four dependent levels plus the length lookups are latency,
  not width. Scalar stays.
- **Four-block joint constraints** on V2 decode: 0.97–1.05 against pairs. Pairs stay.
- **Stitching each block with Bec256's overlapping-store `stitch`**, then
  reloading words: store-forwarding stalls made encode slower than the serial
  writer. Register-chain packing (NEON) or assembled registers (AVX-512) fixed it.
- **Scalar register-chain packing on AVX-512**: encode prepare 329 ns against
  205 with `assemble`.
- **Fresh block access** (no retained frame) pays the head: about 55 ns on V2
  and 33 ns on Zen, so it is 2–7× a retained-frame block access. Callers that
  iterate blocks bind once. A cheaper head would need a different head law.
- **Sparse groups** (few nonterminal blocks) still pay a fixed head/emission
  cost in encode: 1.08–1.12× Bec256 on wikileaks and clustered on V2,
  1.31–1.34× on Zen. Remaining emission costs about 16 ns on Zen and 40 ns on
  V2 for such groups; the head fields alone are 5 / 19 ns.
- **Scalar head fields in encode** (bit widths and table loads per field):
  28 ns on Zen against 5 ns for the vector form (exclusive-prefix gathers);
  zeroing unused packed words on NEON instead of selecting them cost 40 ns.
- Granite Rapids, cold-cache access and length-changing mutation of one block
  inside a body (which shifts every later stream) are not measured. The last is
  the main open design question for writes.

## Code and reproduction

The spike's sources were removed at graduation; commit `a5daf941` holds them,
including the diagnostic `prepare`/`emit`/`head` benchmark arms used for the
encode findings. The maintained code is in `ikea/include/ikea/bec/` (`bec4096.h`,
`author/bec4096.h`, `detail/bec4096.h`) and `ikea/src/bec/bec4096.cpp`, with the
shared level step in `detail/native/{neon,avx512}.h`. Rerun the comparison with
the suite's helper:

```sh
python3 workbench/tools/worker.py run workbench/benchmarks/bec/run.sh \
  --machine neoverse-v2 --arg neon      # or --machine zen5 --arg avx512
python3 workbench/benchmarks/bec/summarise_bec4096.py \
  build/workers/JOB/results/bec4096-samples.json
```

[compare_arms.py](compare_arms.py) compares two sample files (used for the
Bec256 before/after captures). Evidence: [V2](evidence/neoverse-v2/samples.json)
and [Zen 5](evidence/zen5/samples.json) final spike runs (with `sizes.txt`),
and the Bec256 before/after captures in `evidence/bec256-core-{before,after}-*`.
Each directory's `artifact.json` recovers the full worker bundle.
