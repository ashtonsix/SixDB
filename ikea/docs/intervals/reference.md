# Intervals reference

[Usage and obligations](usage.md) describe the maintained API. Endpoints, geometry,
selection and execution remain separate; no source must adopt a compact image.

| Surface | Contract |
| --- | --- |
| `lower_bound`, `upper_bound` | Ordered `size()+at()` source; exact logical comparison and unsigned local rank/sentinel |
| `from_ends`, `from_cuts` | Borrowed geometry interpretation, including an origin and exclusive ends |
| `locate`, `start`, `reverse`, `overlap`, `range` | Checked rank/domain operations; stable zero/duplicate ranks; carried traversal |
| `with_geometry` | Typed complete consumer body, no mandatory flattened wrapper |
| `rank_selection`, `selected_source` | Borrowed operation eligibility; excluded owners do not reassign coordinates |
| `prepare_lower_bound`, `prepare_upper_bound` | Named coherent borrowed source retained through finish; possible strided local read window |
| `direct_source`, `delta_source`, `relative_end_source` | Borrowed endpoints or G16 local fields; explicit initialized/readable grants |
| `sparse_sizes`, `sparse_ends` | Checkpoint spacing independent of native grain; optional readable checkpoint capacity |
| `for_independent_reads` | Opt-in G64 prefix policy, same bytes, serial inverse; qualified workload/ISA tradeoff |
| `run_end_source`, `fixed_source` | Immutable end-minus-one bytes or admitted arithmetic geometry |
| `size_editor` | Borrowed checked append/resize with original-source damage before stores |

`<ikea/intervals/author/search.h>` supplies opt-in prepared numeric bindings,
small hot-list scans and independent search batches. Their models are execution
choices, not ordering or persistent format constraints. Existing generic sources
keep their comparison law. Prepared numeric state becomes stale after any change
to endpoint data, count or placement.

StreamPack owns its compact Absolute Ends / Sizes / Relative Ends byte laws and
legacy/current decoding. [Its reference](../streampack/reference.md) describes
those layouts, liveness, used lengths, payload and effects. Intervals' sparse
sources define no persisted schema. Historical `<ikea/boundlist.h>` headers and
`ikea::boundlist` target are transitional compatibility for old controls, forwarding
to Intervals and StreamPack's internal compact implementation. New consumers use
the actual owner directly. Intervals has no reverse dependency on that route.

The [graduation record](../../../workbench/spikes/intervals/GRADUATION.md) owns
integration evidence; [native performance research](../../../workbench/spikes/intervals/RAISE-BAR.md)
retains winners, losses, disassembly and exact source/binary recovery.
