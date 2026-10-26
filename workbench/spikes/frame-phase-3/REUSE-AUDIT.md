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
| 3 | `intervals` reconstruction | Decoded signed/modular u64 fields from a framed child | `ikea::intervals::wide_sizes` / `wide_ends` | **Routed in the landing.** Intervals owns the borrowed decoded-window reconstruction and ordered search; Frame supplies offset-binary translation and separately framed checkpoints. Nonmonotone reconstruction is distinct from ordered search. Traversal carries region indices (85f22f57), avoiding a division per region. Boundary review and final native qualification are recorded in the graduation receipt |
| 4 | `intervals` sizes SUM | The prototype disabled its masked-per-bit law and decoded instead | SeriesPack exact `weighted_sum`, periods 16/64 | **Routed in the landing.** Frame uses the exact weighted child law through bound compensation and pack. SeriesPack 2731574e owns the native kernel; no modulo-u64 workaround remains in the maintained leaf |
| 5 | Pack range/selected SUM | Prototype chunking and decode above 55-bit widths | SeriesPack exact plain/masked SUM | **Routed in the landing.** The maintained leaf calls compiled exact operations at every positive width. The old manual path remains only in the frozen regression control |
| 6 | `widths.h`, `widths_*.cpp` | Typed width arrays | SeriesPack compiled-once operations | **Ok**. Default build compiles none (`FRAME3_LEAF=runtime`); kept only as the typed comparison control |
| 7 | `bound` fit | Window choice | Bounds thread's `frame_bounds::fast::choose` | **Ok**. Bound's read and write laws are Frame's algebra; no module offers them |
| 8 | `pack` leaf | Reads, points, writes, footprints | SeriesPack `bind_range_reader`, `set`, `footprint` | **Ok** |
| 9 | `split` placeholder ([placeholders.h](proto/placeholders.h)) | Fixed logical high/suffix children | SeriesPack separated heads for the physical H8/H16 packing choice | **Route physical heads; keep meanings distinct.** Pack heads are relative to fitted K (and any preceding bound), whereas logical `split(L)` makes independent child series at a fixed suffix width. Use SeriesPack for head planes; retire a split-as-head workaround, but do not alias the two descriptions or erase the distinct logical split shape |
| 10 | `runs` placeholder | Own decode and laws over run sizes | Intervals `run_end_source` for run points | **Motivated for now**. It is a placeholder with no executable landing; when runs leave research, their point path uses `run_end_source`, as phase 1's point programs already do |
| 11 | `record` placeholder | Shared leaf | TuplePack layout, view, writer | **Ok** |
| 12 | Position-set helpers | Local iteration, intersection and prefix construction | `ikea::bitset256` | **Promoted 22a56d7c.** `for_each`, `operator&` and `first_n` are shared; the prototype forwards to them |
| 13 | Public read selections | Raw private block/row words | Existing Intervals/SeriesPack selectors | **Routed in the landing.** Coarse block coordinates use Intervals selection; fine original rows use SeriesPack selection. SeriesPack also borrows `span<const bitset256>` so FILTER output feeds another consumer without caller flattening |
| 14 | `test(op, x, t)` | Runtime comparison switch | `ikea::comparison` (used), `compare<Op>` (compile-time only) | **Ok**. The enum is reused; a runtime form is a one-line helper |
| 15 | Effects, Orbital, Loom | Journal, ObjectStore, Ring | Their public APIs | **Ok** |
| 16 | Bec | Not used | Compressed bitsets | **Ok**. No stage needs one yet; presence or rank-map stages should store through Bec when they become real |
| 17 | Owner canonical description ([description.cpp](owner/description.cpp)) | Frame graph/parameters/coordinates in a versioned envelope | `ikea::description::write_header` / `read_body` | **Ok.** Both common envelope APIs are used. Bounded graph/body parsing and diagnostic error classification are Frame's composition law, not a second leaf format |
| 18 | Leaf storage extents | Owner duplicated SeriesPack striped tile arithmetic | SeriesPack `layout_value::required_storage(rows)` | **Promoted 22a56d7c and reused.** Runtime geometry/plane extents handle H0/H8/H16, partial tiles and overflow; Frame still checks exact section consumption and parent relationships |
| 19 | Owner group/interval validation ([group_image.cpp](owner/group_image.cpp), [stream_binding.cpp](owner/stream_binding.cpp)) | Exception membership/zero fields, base overflow, checkpoint zero offsets and last/monotone/exact evidence | SeriesPack reader plus shared stage reconstruction, then Frame-specific proof | **Motivated.** It already uses the actual Ikea field decoder. Cross-child aggregate premises are Frame's responsibility: the zero-checkpoint-field counterexample passes ordinary decode. Keep independent proof; route shared reconstruction mechanisms through their modules, without validating a flag by a shortcut that assumes it |
| 20 | Owner selection wrappers ([stream_binding.h](owner/stream_binding.h)) | Coarse `bitset256`, per-block row bits and private coverage checks | Existing Ikea selections and `bitset256`; rows12/13 | **Routed in the landing.** Public readers take native Intervals/SeriesPack selectors, retaining explicit block/original-row origins and full-request coverage/refusal guarantees. A generic bit container does not erase coordinates or grant bytes |
| 21 | Owner capture/lifetime/publication ([material.cpp](owner/material.cpp), [stream_history_check.cpp](owner/stream_history_check.cpp)) | Custom material Control, interpretation and selected-Part retention | Public Orbital Representation/ObjectStore customization | **Ok, integration example.** Catalog, cuts, reuse and adoption already use Orbital; Frame-specific immutable meaning is the supplied material. Keep that glue outside Ikea's library dependency graph |
| 22 | Private change effects ([binding.cpp](owner/binding.cpp)) | Declares composite envelopes, applies through shared stages, resolves/checks issued stores | SeriesPack set/footprint and `ikea::journal` | **Research only; excluded from initial extraction.** Leaf stores/effects are reused. Future changes still need composition-level resource/grant checks; the spike's `frame_change::ranges` helper is not a lock manager or public API to copy verbatim |
| 23 | Native controls ([controls](controls/README.md)) | Genuine typed FOR arrays, count/SUM/pair/mask/gather loops and private admission wrappers | Existing shared `frame_cells_controls` source | **Motivated control; untangle retired-image includes during curation.** A native floor must not acquire Frame encoding/dispatch overhead. Reuse one qualified source and independent per-output oracles; keep private masks out of the public API. `cells.h` still includes retired `image.h` for shared aliases/declarations: separate native contracts before physically removing that historical implementation |
| 24 | Actual system controls ([systems](controls/systems/README.md)) | Exact unsigned query adapters, typed-FOR ingestion, pinned cached peer/source checks and source/input/binary seals | Real DuckDB/Polars plus Workbench peer/artifact/capture facilities | **Motivated benchmark adapter; no library promotion.** These are controls with different query boundaries, not replacement Ikea kernels. Acquisition/retention belongs to Workbench; do not move runner/provider management into Frame |

## Status

Rows 1–5, 12, 13 and 18 now route through their owning Ikea modules in the
read/build landing. This is source reuse, not an automatic inheritance of old
performance numbers: [GRADUATION.md](GRADUATION.md) records combined-source
qualification. Physical head planes and logical split retain distinct meanings;
unsupported images refuse. Frame still owns cross-child proof, authority and
lifetime obligations. Row 22 remains research: checked private preparation and
mutation are excluded. Native controls keep independent oracles and stay outside
the consumer library.
