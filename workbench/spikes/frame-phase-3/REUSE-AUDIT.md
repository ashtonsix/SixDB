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
| 9 | `split` placeholder ([placeholders.h](proto/placeholders.h)) | Fixed logical high/suffix children | SeriesPack separated heads for the physical H8/H16 packing choice | **Route physical heads; keep meanings distinct.** Pack heads are relative to fitted K (and any preceding bound), whereas logical `split(L)` makes independent child series at a fixed suffix width. Use SeriesPack for head planes; retire a split-as-head workaround, but do not alias the two descriptions or erase the distinct logical split shape |
| 10 | `runs` placeholder | Own decode and laws over run sizes | Intervals `run_end_source` for run points | **Motivated for now**. It is a placeholder with no executable landing; when runs leave research, their point path uses `run_end_source`, as phase 1's point programs already do |
| 11 | `record` placeholder | Shared leaf | TuplePack layout, view, writer | **Ok** |
| 12 | `frame.h` bitset helpers | `each`, `&`, `first_n` on `bitset256` | `ikea::bitset256` has none of them | **Strengthen**. Small additions to `bitset256`, for Ikea LEAD |
| 13 | `column` selections | Raw u64 words | SeriesPack and TuplePack `selection::bits` (covers, mask) | **Strengthen at extraction**. The public binding takes Ikea's selection types; the prototype driver is not public |
| 14 | `test(op, x, t)` | Runtime comparison switch | `ikea::comparison` (used), `compare<Op>` (compile-time only) | **Ok**. The enum is reused; a runtime form is a one-line helper |
| 15 | Effects, Orbital, Loom | Journal, ObjectStore, Ring | Their public APIs | **Ok** |
| 16 | Bec | Not used | Compressed bitsets | **Ok**. No stage needs one yet; presence or rank-map stages should store through Bec when they become real |
| 17 | Owner canonical description ([description.cpp](owner/description.cpp)) | Frame graph/parameters/coordinates in a versioned envelope | `ikea::description::write_header` / `read_body` | **Ok.** Both common envelope APIs are used. Bounded graph/body parsing and diagnostic error classification are Frame's composition law, not a second leaf format |
| 18 | Owner leaf extents ([group_image.cpp](owner/group_image.cpp)) | Locally derives SeriesPack tile rows/bytes using the striped GCD formula | SeriesPack `layout<K,G,H>` and runtime dense admission own that geometry | **Strengthen at extraction.** Expose/reuse a runtime geometry/required-plane-bytes query in SeriesPack instead of maintaining a second formula in Frame. Frame still checks exact section consumption and its versioned resolved family; K0 remains its no-leaf case |
| 19 | Owner group/interval validation ([group_image.cpp](owner/group_image.cpp), [stream_binding.cpp](owner/stream_binding.cpp)) | Exception membership/zero fields, base overflow, checkpoint zero offsets and last/monotone/exact evidence | SeriesPack reader plus shared stage reconstruction, then Frame-specific proof | **Motivated.** It already uses the actual Ikea field decoder. Cross-child aggregate premises are Frame's responsibility: the zero-checkpoint-field counterexample passes ordinary decode. Keep independent proof; route shared reconstruction mechanisms through their modules, without validating a flag by a shortcut that assumes it |
| 20 | Owner selection wrappers ([stream_binding.h](owner/stream_binding.h)) | Coarse `bitset256`, per-block row bits and private coverage checks | Existing Ikea selections and `bitset256`; rows12/13 | **Strengthen at extraction.** Reuse the native carrier and selection adapters, retaining explicit block/original-row origins and full-request coverage/refusal guarantees. A generic bit container does not erase coordinates or grant bytes |
| 21 | Owner capture/lifetime/publication ([material.cpp](owner/material.cpp), [stream_history_check.cpp](owner/stream_history_check.cpp)) | Custom material Control, interpretation and selected-Part retention | Public Orbital Representation/ObjectStore customization | **Ok, integration example.** Catalog, cuts, reuse and adoption already use Orbital; Frame-specific immutable meaning is the supplied material. Keep that glue outside Ikea's library dependency graph |
| 22 | Private change effects ([binding.cpp](owner/binding.cpp)) | Declares composite envelopes, applies through shared stages, resolves/checks issued stores | SeriesPack set/footprint and `ikea::journal` | **Ok with extraction cleanup.** Leaf stores/effects are reused. Composition-level resource/grant checks remain required; the spike's `frame_change::ranges` helper is not a new lock manager or a public API to copy verbatim |
| 23 | Native controls ([controls](controls/README.md)) | Genuine typed FOR arrays, count/SUM/pair/mask/gather loops and private admission wrappers | Existing shared `frame_cells_controls` source | **Motivated control; untangle retired-image includes during curation.** A native floor must not acquire Frame encoding/dispatch overhead. Reuse one qualified source and independent per-output oracles; keep private masks out of the public API. `cells.h` still includes retired `image.h` for shared aliases/declarations: separate native contracts before physically removing that historical implementation |
| 24 | Actual system controls ([systems](controls/systems/README.md)) | Exact unsigned query adapters, typed-FOR ingestion, pinned cached peer/source checks and source/input/binary seals | Real DuckDB/Polars plus Workbench peer/artifact/capture facilities | **Motivated benchmark adapter; no library promotion.** These are controls with different query boundaries, not replacement Ikea kernels. Acquisition/retention belongs to Workbench; do not move runner/provider management into Frame |

## Status

Rows 1 and 2 are routed and pass the oracle check (both interval forms, monotone columns). Rows 3, 4, 5, 12, 13 and 18 are
bounded reuse/extraction work through the owning module, not claims that the
enhancements have landed or inherited earlier timings. Rows 19 and 22 preserve
Frame's composition/authority obligations; leaf reuse cannot replace them.
Row 9 keeps physical head planes distinct from a logical two-child split.
Native controls and their independent oracles remain independent of Frame.
