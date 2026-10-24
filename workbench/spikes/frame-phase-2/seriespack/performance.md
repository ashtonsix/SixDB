# SeriesPack performance iteration

This iterates on SeriesPack's own scan kernels, starting from the gaps in the
[remeasure](README.md). Everything lands in `ikea::seriespack` (reader,
author drivers and native groups); the format is unchanged. The harness is
[`measure.cpp`](measure.cpp). The checks are [`checks/`](checks/):
- `kernels.cpp` covers SUM, every comparison's COUNT, filter words and partial ranges;
- `bounds.cpp` covers range reads ending at a guard page, range SUMs, and masked SUMs mixing full and partial groups.

The maintained Ikea checks cover the new operations too. `ikea/test/seriespack/read/range.cpp` puts partial final tiles before a protected page, adds maximum values, every comparison, empty calls on inaccessible bytes, placed, headed, gapped and guarded sources. `operations.h` covers the checked forms, from dense, placed and substituted readers. [`checks/worker.sh`](checks/worker.sh) runs those with the spike checks and fails the job on any failure. They pass on the final sources on Zen 5 (AVX2, AVX-512) and Graviton 4: jobs `20261024T063330Z-64514ca2` and `20261024T063356Z-27f15d47`.

Codex (Ikea LEAD) reviewed the batch
(`build/frame-collab/seriespack-performance-review.md`). It found three bounded
defects, fixed before landing:
- the unconditioned SUM dropped 64-bit maximum values;
- the guarded-expression SUM failed to compile;
- the dense reader lacked the new operations.

Timing jobs on the first batch's final kernel sources (landed as 7d34f479): `20261024T061814Z-d13d707d` (Zen 5,
AVX-512) and `20261024T061851Z-4ecc6c7c` (Graviton 4). The second batch,
striped points and two-byte-body NEON COUNT (reviewed by Codex; checks `20261024T072430Z-0f3315c7` and `20261024T072454Z-86b46a18` pass), was timed in jobs
`20261024T070126Z-edea03ef` (Zen 5) and `20261024T070716Z-62f6198d` (Graviton 4); medians in [evidence/performance-batch2-zen5-avx512.txt](evidence/performance-batch2-zen5-avx512.txt) and [evidence/performance-batch2-graviton4.txt](evidence/performance-batch2-graviton4.txt). Medians are in
[evidence/performance-zen5-avx512.txt](evidence/performance-zen5-avx512.txt)
and [evidence/performance-graviton4.txt](evidence/performance-graviton4.txt).
The "before" figures come from the remeasure jobs in [README.md](README.md) and
from intermediate jobs on earlier kernel sources:
- `20261024T034721Z-de7ed4b3` and `20261024T035623Z-c822ce1f` (Zen 5);
- `20261024T034820Z-27ed3b2b` and `20261024T041001Z-de03c58f` (Graviton 4).

Summaries of those intermediate jobs are not retained. All times are ns per 256 rows, warm unless stated.

## Comparison limits

Graviton 4's native SUM control is slow at about 10–11 ns per 256 rows for
every lane width; `plane256` reaches 2.6–7.6 ns on the same machine. There,
`plane256` is the more useful bar. The flushed condition is a conditioning
step, not proof of DRAM service.

## What changed and what it bought

| Change | Zen 5 before → after | Graviton 4 before → after |
| --- | --- | --- |
| Expression reads always inlined; striped group positions folded per tile (`each_group`) | bulk SUM K 1–7: 5–7× native → 1.25–2.0 (native 2.6) | bulk SUM K 1–7: 6.4–10.5 (native ≈ 10) |
| SUM sums fields apart (`total`: high total shifted plus low total; no join or widen) when a group is complete and unconditioned | bulk K 10–15 7.0 → 3.3; compact K 10–15 9.7 → 5.9 | bulk K 10–15 24–28 → 15–18; compact 40–60 → 33–48 |
| One-pass `count_unchecked` / `filter_words_unchecked` (`filter_regions`: no per-window call) | compact K ≤ 8 COUNT 10–11 → 1.4–2.4; bulk 9–18 → 1.2–1.8 (native 4.6) | bulk 46–60 → 19–22 (native 17) |
| Range `sum_unchecked(first, count)`, Local: plane popcounts plus body byte sums, one pass | compact K ≤ 6 0.20–0.59, K = 7 1.6, K 10–15 1.9–2.6 (native 2.6–2.7; `plane256` 1.2–4.7); K = 20 11.3 → 4.9, K = 24 8.5 → 6.3 (native 5.3) | compact K ≤ 7 1.1–5.3 (`plane256` 2.7–7.6); K 10–15 12–23; K = 24 unchanged at 43 |
| Range SUM, striped: nibble tables over residual stripes | bulk K ≤ 7 1.25–2.0 → 0.31–1.9 | bulk K ≤ 7 7–12 → 1.7–10.5; K 10–20 15–18 → 7.7–15 |
| Bit-sliced COUNT on stored Local planes (NEON; residual-only and one-byte bodies) | — | compact K ≤ 7 26–49 → 1.7–19.3, K 10–15 67–83 → 17.5–32.7 (native 17–19) |
| Striped points at odd widths: a per-group table of the residual's two byte fields replaces a branch on the random group, which mispredicted | bulk K = 3, 5, 6, 7 1.9–3.7 → 1.1–1.2; K = 14, 15 3.2–4.6 → 2.1–2.2 (native 0.26) | bulk K = 3–7 2.8–4.5 → 1.8; K = 14, 15 4.3–5.2 → 3.2–3.3 (native 0.28–0.40) |
| NEON bit-sliced COUNT for two-byte Local bodies (u16 compares narrowed to masks) | — | compact K = 20 103 → 28 (native 23) |
| AVX-512 short reads of at most 64 rows: the covering native groups, written by compressed prefix stores, only where a group stays inside one tile (striped) | bulk 24–33 → 13–24 ns per read (native 16–17); compact keeps partial packets and pays 25–29, up from 24–27, because a packet that would start before the first requested row's tile now falls back to points (the exact containing-tile law); evidence/performance-batch3-zen5-avx512.txt, job `20261024T083648Z-03509033` | — |
| Short reads decode their partial edge packets | — | 10–22% faster locally; 0.9–1.2× native on Graviton 4 |

Flushed, Zen 5 range SUM tracks bandwidth: compact K ≤ 7 takes 0.7–5.4, K 10–15 7.9–10.5 (native 11) and K = 24 23 (native 22, before 57). The group-driver SUM had left compact 2–3.5× behind bulk there.

## Findings

1. **SeriesPack has at least the scan headroom of `plane256`.** Compact range SUM beats `plane256` and native at every K ≤ 15 on Zen 5. Bulk beats native at K ≤ 7, and one-pass COUNT beats native at K ≤ 8 on both layouts. No new format was needed; each gain came from a kernel or driver over the existing bytes.
2. **Driver shape mattered as much as kernels.** The per-window filter call cost 3–7× against the same kernel in a one-pass driver. An outlined expression read cost 7× (Zen 5 striped K = 3 SUM: 9 against 1.3), because the striped group position stops folding and accumulators spill.
3. **Linearity composes; reconstruction does not need to.** SUM over a join is the high child's SUM shifted plus the low child's, so fields are summed apart. At a range, the leaf sum fuses with traversal into a layout kernel: plane popcounts in Local, nibble tables in striped. A prefilter callback hides "every row", so an unconditioned range needs its own entry point (`sum_unchecked(first, count)`).
4. **Local and Bulk favour different operations, so both matter.** Every Local residual byte is one plane of eight rows. That makes popcount SUM and bit-sliced compares run directly on the stored bytes; there compact range SUM beats bulk on both ISAs at K ≤ 7. A striped residual byte mixes planes, which needs table lookups. Bulk keeps the advantage for decode, mixed widths (K 10–20 SUM and COUNT on both machines) and AVX-512 group kernels. This bears on the with-RLE and without-RLE pipelines.
5. **Masked loads are not the cold-scan cost on Zen 5.** Masked and plain 64-byte loads time the same cold and warm (masked-load evidence: [evidence/maskload-zen5.jsonl](evidence/maskload-zen5.jsonl)). The cold compact gap came from the group-driver SUM path itself; the range kernel removes it.
6. **Some routes lose and stay off.** On AVX-512, the striped range kernel at body-bearing widths mixes roles within vectors and runs slower than the group driver (K = 12: 4.6 against 3.3), so it is used there only at residual-only widths.

## One decode, many consumers

Ashton's multi-yield idea is now `author::scan_regions(source, first, count, group, edge)`. The source is decoded once, in the widest native group (16-row packets on AVX2 alone), and every consumer composed inside `group` reads the same registers. [`yield/bench.cpp`](yield/bench.cpp) feeds four consumers: SUM, COUNT ≥ t, COUNT = t and MAX. It compares that against separate passes (range SUM kernel, two one-pass counts and a MAX-only scan). Every timed arm returns all four results, after Codex found an earlier version let the compiler drop unobserved consumers.

| ns per 256 rows, warm | Zen 5 fused | Zen 5 separate | Graviton 4 fused | Graviton 4 separate |
| --- | --- | --- | --- | --- |
| compact K = 3, 7, 8 | 2.0–3.2 | 4.8–7.9 | 17–51 | 26–64 |
| compact K = 12 | 14.3 | 20.0 | 74 | 100 |
| bulk K = 3, 7 | 2.9–3.1 | 5.2–6.7 | 20–24 | 55–65 |
| bulk K = 12, 14 | 8.8 | 13.8 | 38–39 | 105–109 |

Evidence: [evidence/yield-zen5.jsonl](evidence/yield-zen5.jsonl) (job `20261024T081714Z-f1bc8f9b`) and [evidence/yield-graviton4.jsonl](evidence/yield-graviton4.jsonl) (job `20261024T081810Z-af5439a6`).

- One decode feeding four consumers beats separate passes on Zen 5 everywhere, by 1.4–3.1×. On Graviton 4 it wins by 1.3–3.2×, except compact K = 3, where the plane-popcount SUM and bit-sliced counts already avoid decoding (40 against 26).
- The bench's native arm, the same four consumers fused over the byte-lane array, is a scalar loop the compiler does not vectorise once every result is used: 18 ns per 256 rows on Zen 5 and 137–151 on Graviton 4. It is not a bar for this comparison.
- On NEON, consumers should count in lane masks (a compare and a widening add). On my local Arm machine that made the fused scan 1.6–6× faster than per-group bit words. The standalone one-pass COUNT is the exception: bit words won there, so the reader keeps them.

## Compose or fuse: what this suggests for Engine

- Compose at the expression level: fields, joins, compares and SUM's linearity. These are cheap to keep general when everything inlines.
- Fuse at the range level, per layout. Traversal, the leaf read and the reduction become one kernel when the operation and its admission are known for a whole range: an unconditioned SUM, or a comparison against a constant. The decision Engine owns is to say so, passing "all rows" and the operation explicitly rather than per-group callbacks.
- Layout choice is per operation mix: Local for scan-heavy residual-only columns, Bulk for decode, points at standard widths and mixed widths.

## Remaining gaps

- Graviton 4 compact decode (`read_u32`) runs 1.5–3.5× native at non-byte widths, and bulk decode 1.6–1.8× at K 10–20. [`decode/bench.cpp`](decode/bench.cpp) (job `20261024T074015Z-6b0665db`, [evidence](evidence/decode-graviton4.jsonl)) splits the cost. The rebuild alone takes 10–29 ns per 256 rows against a 21 ns widening-store floor, so K ≥ 5 is bound by rebuilding the bits. Spreading each plane byte across lanes instead runs 1.3–2.6× slower than the shipped transpose, and Bulk avoids the transpose altogether.
- Bulk points at odd striped widths still run 4–8× native (compact 2–3.6×). Points need at least two dependent loads (body and a stripe) per row.
- On NEON, two-byte-body Local range SUM (K 17–23) stays on the group driver: masking each body byte apart lost there (locally 26–40 against 18–29).
- Short reads: compact 1.3–1.5× native on Zen 5 (21–25 ns after the partial packets moved to compressed lane-masked stores, from 25–29; [evidence/performance-batch4-zen5-avx512.txt](evidence/performance-batch4-zen5-avx512.txt), job `20261024T223805Z-7900c71d`) and 1.1–1.2× on Graviton 4. Local reads starting in the second half of a 16-row packet take up to seven points under the containing-tile law.
- Bulk points: an arithmetic two-load residual for widths 5 and 7, replacing the field table, ran about 20% slower locally and was reverted.
