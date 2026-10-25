# Submodule reuse audit

25 October, at Ashton's request: where does Frame phase 3 rebuild something
an Ikea module already provides, and is the bypass motivated? This file covers
the stage side ([proto/](proto/)). SeriesPack findings come from the "Speed
up SeriesPack operations" thread. Ikea LEAD audits [owner/](owner/) and
[controls/](controls/).

Verdicts: **routed** (now calls the module), **strengthen** (the module falls
short, so the gap goes to its owner rather than staying in Frame),
**motivated** (evidence or the module's own contract justifies it),
**retire**, or **ok** (already reused).

| # | Where | What Frame does | Module that covers it | Verdict |
| --- | --- | --- | --- | --- |
| 1 | `intervals::count` ([stages.h](proto/stages.h)) | `std::lower_bound` over decoded checkpoints | `ikea::intervals::lower_bound` over `direct_source` | **Routed**. No reason found for the bypass |
| 2 | `intervals::count` | Scalar loop counting a region's values below t | The same endpoint search over the reconstructed region | **Routed** |
| 3 | `intervals::decode` / `get` (sizes form) | Prefix of deltas within each G-row region | `sparse_sizes` / `delta_source` reconstruction | **Strengthen**. Intervals' sources take nonnegative u8/u16 native planes with 32-bit prefix sums. Frame's deltas are offset-binary signed u64 fields of a framed child. Intervals' own [caller audit](../intervals/AUDIT.md) leaves signed reconstruction to Frame, but no measurement motivates keeping it there. Proposal: Intervals gains a source over a decoded wide window (u64 fields, modular prefix), so Frame's sizes form reconstructs through Intervals, and monotone narrow columns can be steered to Intervals' native planes directly |
| 4 | `intervals::sum` (sizes form) | Masked-sum law built from `sum_pattern`, switched off, so SUM decodes | SeriesPack `weighted_sum` (5–10× faster on Zen 5, 3.8–11× on Graviton 4) | **Strengthen**. It sums modulo 2^64 with period-16 weights; Frame needs an exact u128 result and period 64 for G64. SeriesPack's owner is adding both, then Frame switches and deletes the masked law |
| 5 | `runtime_width` range and masked sums | Chunks to exact rows and decodes above 55-bit widths | SeriesPack range SUM | **Motivated, then strengthen**. SeriesPack sums modulo 2^64. Its owner is adding an exact wide SUM; until then URLHash SUM is 2.6× slower |
| 6 | `widths.h`, `widths_*.cpp` | Typed width arrays | SeriesPack compiled-once operations | **Ok**. Default build compiles none (`FRAME3_LEAF=runtime`); kept only as the typed comparison control |
| 7 | `bound` fit | Window choice | Bounds thread's `frame_bounds::fast::choose` | **Ok**. Bound's read and write laws are Frame's algebra; no module offers them |
| 8 | `pack` leaf | Reads, points, writes, footprints | SeriesPack `bind_range_reader`, `set`, `footprint` | **Ok** |
| 9 | `split` placeholder ([placeholders.h](proto/placeholders.h)) | Head and suffix as two Frame children | SeriesPack's separated heads (`layout_value{width, heads, storage}`, H8/H16) | **Retire**. Head separation is `pack` with SeriesPack heads, which Ikea LEAD's descriptions already name |
| 10 | `runs` placeholder | Own decode and laws over run sizes | Intervals `run_end_source` for run points | **Motivated for now**. It is a placeholder with no executable landing; when runs leave research, their point path uses `run_end_source`, as phase 1's point programs already do |
| 11 | `record` placeholder | Shared leaf | TuplePack layout, view, writer | **Ok** |
| 12 | `frame.h` bitset helpers | `each`, `&`, `first_n` on `bitset256` | `ikea::bitset256` has none of them | **Strengthen**. Small additions to `bitset256`, for Ikea LEAD |
| 13 | `column` selections | Raw u64 words | SeriesPack and TuplePack `selection::bits` (covers, mask) | **Strengthen at extraction**. The public binding takes Ikea's selection types; the prototype driver is not public |
| 14 | `test(op, x, t)` | Runtime comparison switch | `ikea::comparison` (used), `compare<Op>` (compile-time only) | **Ok**. The enum is reused; a runtime form is a one-line helper |
| 15 | Effects, Orbital, Loom | Journal, ObjectStore, Ring | Their public APIs | **Ok** |
| 16 | Bec | Not used | Compressed bitsets | **Ok**. No stage needs one yet; presence or rank-map stages should store through Bec when they become real |

## Status

Rows 1 and 2 are routed in the working tree. Rows 3, 4, 5, 12 and 13 must be
resolved before the first Ikea batch, through their owners. Rows 9 and 10
change what stays in research.
